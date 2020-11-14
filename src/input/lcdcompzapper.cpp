/* FCE Ultra - NES/Famicom Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>

#include <thread>

#include "remotegun.grpc.pb.h"
#include "share.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using remote::RemoteGun;
using remote::FireMessage;
using remote::FireAcknowledged;

static uint32 lcdCompZapperStrobe[2];
static uint32 lcdCompZapperData[2];

const uint8 REMOTEGUN_LIGHT_ON = 0;
const uint8 REMOTEGUN_TRIGGER_ON = 24;
const uint8 REMOTEGUN_BOTH_ON = 16;
const uint8 REMOTEGUN_BOTH_OFF = 8;

enum RemoteGunStateInternal {
    Pass, Miss, Hit
};

enum RemoteGunHitState {
    A, B, C, D
};

class RemoteGunState {
    bool hitProcess;
    RemoteGunHitState hitState;
    RemoteGunStateInternal state;
    int count;

public:
    void Hit() {
        this->count = 0;
        this->state = RemoteGunStateInternal::Hit;
        this->hitState = RemoteGunHitState::A;
    }

    void Miss() {
        this->count = 0;
        this->state = RemoteGunStateInternal::Miss;
        this->hitState = RemoteGunHitState::A;
    }

    bool HasResponse() {
        return (this->state != RemoteGunStateInternal::Pass);
    }

    uint8 Response() {
        if (this->state == RemoteGunStateInternal::Miss)
            return this->MissSequence();

        if (this->state == RemoteGunStateInternal::Hit)
            return this->HitSequence();

        return REMOTEGUN_BOTH_OFF;
    }

private:
    uint8 MissSequence() {
        this->count++;
        if (this->count == 45) {
            this->state = RemoteGunStateInternal::Pass;
            this->count = 0;
        }

        return REMOTEGUN_TRIGGER_ON;
    }

    uint8 HitSequence() {
        this->count++;

        if (this->hitState == RemoteGunHitState::A) {
            if (count == 45) {
                this->hitState = RemoteGunHitState::B;
                this->count = 0;
            }

            return REMOTEGUN_TRIGGER_ON;
        }

        if (this->hitState == RemoteGunHitState::B) {
            if (count == 2750) {
                this->hitState = RemoteGunHitState::C;
                this->count = 0;
            }

            return REMOTEGUN_BOTH_OFF;
        }

        if (this->hitState == RemoteGunHitState::C) {
            this->hitState = RemoteGunHitState::D;
            this->count = 0;

            return REMOTEGUN_LIGHT_ON;
        }

        if (this->hitState == RemoteGunHitState::D) {
            this->state = RemoteGunStateInternal::Pass;

            return REMOTEGUN_BOTH_OFF;
        }

        // shouldn't really happen
        return REMOTEGUN_BOTH_OFF;
    }
};

static RemoteGunState RGState;

class RemoteGunService final : public RemoteGun::Service {
    Status Fire(ServerContext* context, const FireMessage* message, FireAcknowledged* reply) override {
        if (message->is_hit())
            RGState.Hit();
        else
            RGState.Miss();

        return Status::OK;
    }
};

void RemoteGun_StartListener()
{
    RemoteGunService service;
    ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort("0.0.0.0:1109", grpc::InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service);
    // Finally assemble the server.
    std::unique_ptr<Server> server(builder.BuildAndStart());

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

static uint8 ReadLCDCompZapper(int w)
{
	if (lcdCompZapperData[w] == REMOTEGUN_TRIGGER_ON)
	    RGState.Hit();

	return RGState.HasResponse()
	    ? RGState.Response()
	    : lcdCompZapperData[w];
}

static void StrobeLCDCompZapper(int w)
{
	lcdCompZapperStrobe[w] = 0;
}

void UpdateLCDCompZapper(int w, void* data, int arg)
{
	// In the '(*(uint32*)data)' variable, bit 0 holds the trigger value and bit 1 holds the light sense value.
	// Ultimately this needs to be converted from 0000 00lt to 000t l000 where l is the light bit and t
	// is the trigger bit.
	// l must be inverted because 0: detected; 1: not detected
	lcdCompZapperData[w] = ((((*(uint32*)data) & 1) << 4) | 
		                (((*(uint32*)data) & 2 ^ 2) << 2));
}

static INPUTC LCDCompZapperCtrl = { ReadLCDCompZapper,0,StrobeLCDCompZapper,UpdateLCDCompZapper,0,0 };

INPUTC* FCEU_InitLCDCompZapper(int w)
{
    // configure gRPC
    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    // start the gRPC process on a background thread
    std::thread backgroundThread(RemoteGun_StartListener);
    backgroundThread.detach();

	lcdCompZapperStrobe[w] = lcdCompZapperData[w] = 0;
	return(&LCDCompZapperCtrl);
}
