// GameApp.cpp
//
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>

#include "../../fceu.h"
#include "../../fds.h"
#include "../../movie.h"

#ifdef _S9XLUA_H
#include "../../fceulua.h"
#endif

#include "Qt/main.h"
#include "Qt/dface.h"
#include "Qt/input.h"
#include "Qt/ConsoleWindow.h"
#include "Qt/GamePadConf.h"
#include "Qt/HotKeyConf.h"
#include "Qt/PaletteConf.h"
#include "Qt/GuiConf.h"
#include "Qt/MoviePlay.h"
#include "Qt/MovieOptions.h"
#include "Qt/LuaControl.h"
#include "Qt/CheatsConf.h"
#include "Qt/GameGenie.h"
#include "Qt/HexEditor.h"
#include "Qt/TraceLogger.h"
#include "Qt/CodeDataLogger.h"
#include "Qt/ConsoleDebugger.h"
#include "Qt/ConsoleUtilities.h"
#include "Qt/ConsoleSoundConf.h"
#include "Qt/ConsoleVideoConf.h"
#include "Qt/AboutWindow.h"
#include "Qt/fceuWrapper.h"
#include "Qt/ppuViewer.h"
#include "Qt/NameTableViewer.h"
#include "Qt/iNesHeaderEditor.h"
#include "Qt/RamWatch.h"
#include "Qt/RamSearch.h"
#include "Qt/keyscan.h"
#include "Qt/nes_shm.h"

consoleWin_t::consoleWin_t(QWidget *parent)
	: QMainWindow( parent )
{
	int use_SDL_video = false;
   int setFullScreen = false;

	this->resize( 512, 512 );

	g_config->getOption( "SDL.Fullscreen", &setFullScreen );
	g_config->setOption( "SDL.Fullscreen", 0 ); // Reset full screen config parameter to false so it is never saved this way

	if ( setFullScreen )
	{
		this->showFullScreen();
	}

	createMainMenu();

	g_config->getOption( "SDL.VideoDriver", &use_SDL_video );

	errorMsgValid = false;
	viewport_GL  = NULL;
	viewport_SDL = NULL;

	if ( use_SDL_video )
	{
		viewport_SDL = new ConsoleViewSDL_t(this);

   	setCentralWidget(viewport_SDL);
	}
	else
	{
		viewport_GL = new ConsoleViewGL_t(this);

   	setCentralWidget(viewport_GL);
	}

   setWindowIcon(QIcon(":fceux1.png"));

	gameTimer  = new QTimer( this );
	mutex      = new QMutex( QMutex::Recursive );
	emulatorThread = new emulatorThread_t();

   connect(emulatorThread, &QThread::finished, emulatorThread, &QObject::deleteLater);

	connect( gameTimer, &QTimer::timeout, this, &consoleWin_t::updatePeriodic );

	gameTimer->setTimerType( Qt::PreciseTimer );
	//gameTimer->start( 16 ); // 60hz
	gameTimer->start( 8 ); // 120hz

	emulatorThread->start();

}

consoleWin_t::~consoleWin_t(void)
{
	nes_shm->runEmulator = 0;

	gameTimer->stop(); 

	closeGamePadConfWindow();

	//printf("Thread Finished: %i \n", gameThread->isFinished() );
	emulatorThread->quit();
	emulatorThread->wait( 1000 );

	fceuWrapperLock();
	fceuWrapperClose();
	fceuWrapperUnLock();

	if ( viewport_GL != NULL )
	{
		delete viewport_GL; viewport_GL = NULL;
	}
	if ( viewport_SDL != NULL )
	{
		delete viewport_SDL; viewport_SDL = NULL;
	}
	delete mutex;

	// LoadGame() checks for an IP and if it finds one begins a network session
	// clear the NetworkIP field so this doesn't happen unintentionally
	g_config->setOption ("SDL.NetworkIP", "");
	g_config->save ();

	if ( this == consoleWindow )
	{
		consoleWindow = NULL;
	}

}

void consoleWin_t::setCyclePeriodms( int ms )
{
	// If timer is already running, it will be restarted.
	gameTimer->start( ms );
   
	//printf("Period Set to: %i ms \n", ms );
}

void consoleWin_t::showErrorMsgWindow()
{
	QMessageBox msgBox(this);

	fceuWrapperLock();
	msgBox.setIcon( QMessageBox::Critical );
	msgBox.setText( tr(errorMsg.c_str()) );
	errorMsg.clear();
	fceuWrapperUnLock();
	msgBox.show();
	msgBox.exec();
}

void consoleWin_t::QueueErrorMsgWindow( const char *msg )
{
	errorMsg.append( msg );
	errorMsg.append("\n");
	errorMsgValid = true;
}

void consoleWin_t::closeEvent(QCloseEvent *event)
{
   //printf("Main Window Close Event\n");
	closeGamePadConfWindow();

   event->accept();

	closeApp();
}

void consoleWin_t::keyPressEvent(QKeyEvent *event)
{
   //printf("Key Press: 0x%x \n", event->key() );
	pushKeyEvent( event, 1 );
}

void consoleWin_t::keyReleaseEvent(QKeyEvent *event)
{
   //printf("Key Release: 0x%x \n", event->key() );
	pushKeyEvent( event, 0 );
}

//---------------------------------------------------------------------------
void consoleWin_t::createMainMenu(void)
{
   QAction *act;
	QMenu *subMenu;
	QActionGroup *group;
	int useNativeMenuBar;

   // This is needed for menu bar to show up on MacOS
	g_config->getOption( "SDL.UseNativeMenuBar", &useNativeMenuBar );

	menuBar()->setNativeMenuBar( useNativeMenuBar ? true : false );

	 //-----------------------------------------------------------------------
	 // File
    fileMenu = menuBar()->addMenu(tr("File"));

	 // File -> Open ROM
	 openROM = new QAction(tr("Open ROM"), this);
    openROM->setShortcuts(QKeySequence::Open);
    openROM->setStatusTip(tr("Open ROM File"));
    connect(openROM, SIGNAL(triggered()), this, SLOT(openROMFile(void)) );

    fileMenu->addAction(openROM);

	 // File -> Close ROM
	 closeROM = new QAction(tr("Close ROM"), this);
    closeROM->setShortcut( QKeySequence(tr("Ctrl+C")));
    closeROM->setStatusTip(tr("Close Loaded ROM"));
    connect(closeROM, SIGNAL(triggered()), this, SLOT(closeROMCB(void)) );

    fileMenu->addAction(closeROM);

    fileMenu->addSeparator();

	 // File -> Play NSF
	 playNSF = new QAction(tr("Play NSF"), this);
    playNSF->setShortcut( QKeySequence(tr("Ctrl+N")));
    playNSF->setStatusTip(tr("Play NSF"));
    connect(playNSF, SIGNAL(triggered()), this, SLOT(loadNSF(void)) );

    fileMenu->addAction(playNSF);

    fileMenu->addSeparator();

	 // File -> Load State From
	 loadStateAct = new QAction(tr("Load State From"), this);
    //loadStateAct->setShortcut( QKeySequence(tr("Ctrl+N")));
    loadStateAct->setStatusTip(tr("Load State From"));
    connect(loadStateAct, SIGNAL(triggered()), this, SLOT(loadStateFrom(void)) );

    fileMenu->addAction(loadStateAct);

	 // File -> Save State As
	 saveStateAct = new QAction(tr("Save State As"), this);
    //loadStateAct->setShortcut( QKeySequence(tr("Ctrl+N")));
    saveStateAct->setStatusTip(tr("Save State As"));
    connect(saveStateAct, SIGNAL(triggered()), this, SLOT(saveStateAs(void)) );

    fileMenu->addAction(saveStateAct);

	 // File -> Quick Load
	 quickLoadAct = new QAction(tr("Quick Load"), this);
    quickLoadAct->setShortcut( QKeySequence(tr("F7")));
    quickLoadAct->setStatusTip(tr("Quick Load"));
    connect(quickLoadAct, SIGNAL(triggered()), this, SLOT(quickLoad(void)) );

    fileMenu->addAction(quickLoadAct);

	 // File -> Quick Save
	 quickSaveAct = new QAction(tr("Quick Save"), this);
    quickSaveAct->setShortcut( QKeySequence(tr("F5")));
    quickSaveAct->setStatusTip(tr("Quick Save"));
    connect(quickSaveAct, SIGNAL(triggered()), this, SLOT(quickSave(void)) );

    fileMenu->addAction(quickSaveAct);

	 // File -> Change State
	 subMenu = fileMenu->addMenu(tr("Change State"));
	 group   = new QActionGroup(this);

	 group->setExclusive(true);

	 for (int i=0; i<10; i++)
	 {
		 char stmp[8];

		 sprintf( stmp, "%i", i );

		 state[i] = new QAction(tr(stmp), this);
		 state[i]->setCheckable(true);

		 group->addAction(state[i]);
	 	 subMenu->addAction(state[i]);
	 }
	 state[0]->setChecked(true);

    connect(state[0], SIGNAL(triggered()), this, SLOT(changeState0(void)) );
    connect(state[1], SIGNAL(triggered()), this, SLOT(changeState1(void)) );
    connect(state[2], SIGNAL(triggered()), this, SLOT(changeState2(void)) );
    connect(state[3], SIGNAL(triggered()), this, SLOT(changeState3(void)) );
    connect(state[4], SIGNAL(triggered()), this, SLOT(changeState4(void)) );
    connect(state[5], SIGNAL(triggered()), this, SLOT(changeState5(void)) );
    connect(state[6], SIGNAL(triggered()), this, SLOT(changeState6(void)) );
    connect(state[7], SIGNAL(triggered()), this, SLOT(changeState7(void)) );
    connect(state[8], SIGNAL(triggered()), this, SLOT(changeState8(void)) );
    connect(state[9], SIGNAL(triggered()), this, SLOT(changeState9(void)) );

    fileMenu->addSeparator();

#ifdef _S9XLUA_H
    // File -> Quick Save
	 loadLuaAct = new QAction(tr("Load Lua Script"), this);
    //loadLuaAct->setShortcut( QKeySequence(tr("F5")));
    loadLuaAct->setStatusTip(tr("Load Lua Script"));
    connect(loadLuaAct, SIGNAL(triggered()), this, SLOT(loadLua(void)) );

    fileMenu->addAction(loadLuaAct);

    fileMenu->addSeparator();
#else
    loadLuaAct = NULL;
#endif

	 // File -> Screenshort
	 scrShotAct = new QAction(tr("Screenshot"), this);
    scrShotAct->setShortcut( QKeySequence(tr("F12")));
    scrShotAct->setStatusTip(tr("Screenshot"));
    connect(scrShotAct, SIGNAL(triggered()), this, SLOT(takeScreenShot()));

    fileMenu->addAction(scrShotAct);

	 // File -> Quit
	 quitAct = new QAction(tr("Quit"), this);
    quitAct->setShortcut( QKeySequence(tr("Ctrl+Q")));
    quitAct->setStatusTip(tr("Quit the Application"));
    connect(quitAct, SIGNAL(triggered()), this, SLOT(closeApp()));

    fileMenu->addAction(quitAct);

	 //-----------------------------------------------------------------------
	 // Options
    optMenu = menuBar()->addMenu(tr("Options"));

	 // Options -> GamePad Config
	 gamePadConfig = new QAction(tr("GamePad Config"), this);
    //gamePadConfig->setShortcut( QKeySequence(tr("Ctrl+C")));
    gamePadConfig->setStatusTip(tr("GamePad Configure"));
    connect(gamePadConfig, SIGNAL(triggered()), this, SLOT(openGamePadConfWin(void)) );

    optMenu->addAction(gamePadConfig);

	 // Options -> Sound Config
	 gameSoundConfig = new QAction(tr("Sound Config"), this);
    //gameSoundConfig->setShortcut( QKeySequence(tr("Ctrl+C")));
    gameSoundConfig->setStatusTip(tr("Sound Configure"));
    connect(gameSoundConfig, SIGNAL(triggered()), this, SLOT(openGameSndConfWin(void)) );

    optMenu->addAction(gameSoundConfig);

	 // Options -> Video Config
	 gameVideoConfig = new QAction(tr("Video Config"), this);
    //gameVideoConfig->setShortcut( QKeySequence(tr("Ctrl+C")));
    gameVideoConfig->setStatusTip(tr("Video Preferences"));
    connect(gameVideoConfig, SIGNAL(triggered()), this, SLOT(openGameVideoConfWin(void)) );

    optMenu->addAction(gameVideoConfig);

	 // Options -> HotKey Config
	 hotkeyConfig = new QAction(tr("Hotkey Config"), this);
    //hotkeyConfig->setShortcut( QKeySequence(tr("Ctrl+C")));
    hotkeyConfig->setStatusTip(tr("Hotkey Configure"));
    connect(hotkeyConfig, SIGNAL(triggered()), this, SLOT(openHotkeyConfWin(void)) );

    optMenu->addAction(hotkeyConfig);

	 // Options -> Palette Config
	 paletteConfig = new QAction(tr("Palette Config"), this);
    //paletteConfig->setShortcut( QKeySequence(tr("Ctrl+C")));
    paletteConfig->setStatusTip(tr("Palette Configure"));
    connect(paletteConfig, SIGNAL(triggered()), this, SLOT(openPaletteConfWin(void)) );

    optMenu->addAction(paletteConfig);

	 // Options -> GUI Config
	 guiConfig = new QAction(tr("GUI Config"), this);
    //guiConfig->setShortcut( QKeySequence(tr("Ctrl+C")));
    guiConfig->setStatusTip(tr("GUI Configure"));
    connect(guiConfig, SIGNAL(triggered()), this, SLOT(openGuiConfWin(void)) );

    optMenu->addAction(guiConfig);

	 // Options -> Movie Options
	 movieConfig = new QAction(tr("Movie Options"), this);
    //movieConfig->setShortcut( QKeySequence(tr("Ctrl+C")));
    movieConfig->setStatusTip(tr("Movie Options"));
    connect(movieConfig, SIGNAL(triggered()), this, SLOT(openMovieOptWin(void)) );

    optMenu->addAction(movieConfig);

	 // Options -> Auto-Resume
	 autoResume = new QAction(tr("Auto-Resume Play"), this);
    //autoResume->setShortcut( QKeySequence(tr("Ctrl+C")));
    autoResume->setCheckable(true);
    autoResume->setStatusTip(tr("Auto-Resume Play"));
    connect(autoResume, SIGNAL(triggered()), this, SLOT(toggleAutoResume(void)) );

    optMenu->addAction(autoResume);

    optMenu->addSeparator();

	 // Options -> Full Screen
	 fullscreen = new QAction(tr("Fullscreen"), this);
    fullscreen->setShortcut( QKeySequence(tr("Alt+Return")));
    //fullscreen->setCheckable(true);
    fullscreen->setStatusTip(tr("Fullscreen"));
    connect(fullscreen, SIGNAL(triggered()), this, SLOT(toggleFullscreen(void)) );

    optMenu->addAction(fullscreen);

	 //-----------------------------------------------------------------------
	 // Emulation
    emuMenu = menuBar()->addMenu(tr("Emulation"));

	 // Emulation -> Power
	 powerAct = new QAction(tr("Power"), this);
    //powerAct->setShortcut( QKeySequence(tr("Ctrl+P")));
    powerAct->setStatusTip(tr("Power On Console"));
    connect(powerAct, SIGNAL(triggered()), this, SLOT(powerConsoleCB(void)) );

    emuMenu->addAction(powerAct);

	 // Emulation -> Reset
	 resetAct = new QAction(tr("Reset"), this);
    //resetAct->setShortcut( QKeySequence(tr("Ctrl+R")));
    resetAct->setStatusTip(tr("Reset Console"));
    connect(resetAct, SIGNAL(triggered()), this, SLOT(consoleHardReset(void)) );

    emuMenu->addAction(resetAct);

	 // Emulation -> Soft Reset
	 sresetAct = new QAction(tr("Soft Reset"), this);
    //sresetAct->setShortcut( QKeySequence(tr("Ctrl+R")));
    sresetAct->setStatusTip(tr("Soft Reset of Console"));
    connect(sresetAct, SIGNAL(triggered()), this, SLOT(consoleSoftReset(void)) );

    emuMenu->addAction(sresetAct);

	 // Emulation -> Pause
	 pauseAct = new QAction(tr("Pause"), this);
    pauseAct->setShortcut( QKeySequence(tr("Pause")));
    pauseAct->setStatusTip(tr("Pause Console"));
    connect(pauseAct, SIGNAL(triggered()), this, SLOT(consolePause(void)) );

    emuMenu->addAction(pauseAct);

    emuMenu->addSeparator();

	 // Emulation -> Enable Game Genie
	 gameGenieAct = new QAction(tr("Enable Game Genie"), this);
    //gameGenieAct->setShortcut( QKeySequence(tr("Ctrl+G")));
    gameGenieAct->setCheckable(true);
    gameGenieAct->setStatusTip(tr("Enable Game Genie"));
    connect(gameGenieAct, SIGNAL(triggered(bool)), this, SLOT(toggleGameGenie(bool)) );

	 syncActionConfig( gameGenieAct, "SDL.GameGenie" );

    emuMenu->addAction(gameGenieAct);

	 // Emulation -> Load Game Genie ROM
	 loadGgROMAct = new QAction(tr("Load Game Genie ROM"), this);
    //loadGgROMAct->setShortcut( QKeySequence(tr("Ctrl+G")));
    loadGgROMAct->setStatusTip(tr("Load Game Genie ROM"));
    connect(loadGgROMAct, SIGNAL(triggered()), this, SLOT(loadGameGenieROM(void)) );

    emuMenu->addAction(loadGgROMAct);

    emuMenu->addSeparator();

	 // Emulation -> Insert Coin
	 insCoinAct = new QAction(tr("Insert Coin"), this);
    //insCoinAct->setShortcut( QKeySequence(tr("Ctrl+G")));
    insCoinAct->setStatusTip(tr("Insert Coin"));
    connect(insCoinAct, SIGNAL(triggered()), this, SLOT(insertCoin(void)) );

    emuMenu->addAction(insCoinAct);

    emuMenu->addSeparator();

	 // Emulation -> FDS
	 subMenu = emuMenu->addMenu(tr("FDS"));

	 // Emulation -> FDS -> Switch Disk
	 fdsSwitchAct = new QAction(tr("Switch Disk"), this);
    //fdsSwitchAct->setShortcut( QKeySequence(tr("Ctrl+G")));
    fdsSwitchAct->setStatusTip(tr("Switch Disk"));
    connect(fdsSwitchAct, SIGNAL(triggered()), this, SLOT(fdsSwitchDisk(void)) );

    subMenu->addAction(fdsSwitchAct);

	 // Emulation -> FDS -> Eject Disk
	 fdsEjectAct = new QAction(tr("Eject Disk"), this);
    //fdsEjectAct->setShortcut( QKeySequence(tr("Ctrl+G")));
    fdsEjectAct->setStatusTip(tr("Eject Disk"));
    connect(fdsEjectAct, SIGNAL(triggered()), this, SLOT(fdsEjectDisk(void)) );

    subMenu->addAction(fdsEjectAct);

	 // Emulation -> FDS -> Load BIOS
	 fdsLoadBiosAct = new QAction(tr("Load BIOS"), this);
    //fdsLoadBiosAct->setShortcut( QKeySequence(tr("Ctrl+G")));
    fdsLoadBiosAct->setStatusTip(tr("Load BIOS"));
    connect(fdsLoadBiosAct, SIGNAL(triggered()), this, SLOT(fdsLoadBiosFile(void)) );

    subMenu->addAction(fdsLoadBiosAct);

    emuMenu->addSeparator();

    // Emulation -> Speed
	 subMenu = emuMenu->addMenu(tr("Speed"));

	 // Emulation -> Speed -> Speed Up
	 act = new QAction(tr("Speed Up"), this);
    act->setShortcut( QKeySequence(tr("=")));
    act->setStatusTip(tr("Speed Up"));
    connect(act, SIGNAL(triggered()), this, SLOT(emuSpeedUp(void)) );

    subMenu->addAction(act);

    // Emulation -> Speed -> Slow Down
	 act = new QAction(tr("Slow Down"), this);
    act->setShortcut( QKeySequence(tr("-")));
    act->setStatusTip(tr("Slow Down"));
    connect(act, SIGNAL(triggered()), this, SLOT(emuSlowDown(void)) );

    subMenu->addAction(act);

    subMenu->addSeparator();

    // Emulation -> Speed -> Slowest Speed
	 act = new QAction(tr("Slowest"), this);
    //act->setShortcut( QKeySequence(tr("-")));
    act->setStatusTip(tr("Slowest"));
    connect(act, SIGNAL(triggered()), this, SLOT(emuSlowestSpd(void)) );

    subMenu->addAction(act);

    // Emulation -> Speed -> Normal Speed
	 act = new QAction(tr("Normal"), this);
    //act->setShortcut( QKeySequence(tr("-")));
    act->setStatusTip(tr("Normal"));
    connect(act, SIGNAL(triggered()), this, SLOT(emuNormalSpd(void)) );

    subMenu->addAction(act);

    // Emulation -> Speed -> Fastest Speed
	 act = new QAction(tr("Turbo"), this);
    //act->setShortcut( QKeySequence(tr("-")));
    act->setStatusTip(tr("Turbo (Fastest)"));
    connect(act, SIGNAL(triggered()), this, SLOT(emuFastestSpd(void)) );

    subMenu->addAction(act);

    // Emulation -> Speed -> Custom Speed
	 act = new QAction(tr("Custom"), this);
    //act->setShortcut( QKeySequence(tr("-")));
    act->setStatusTip(tr("Custom"));
    connect(act, SIGNAL(triggered()), this, SLOT(emuCustomSpd(void)) );

    subMenu->addAction(act);

    subMenu->addSeparator();

    // Emulation -> Speed -> Set Frame Advance Delay
	 act = new QAction(tr("Set Frame Advance Delay"), this);
    //act->setShortcut( QKeySequence(tr("-")));
    act->setStatusTip(tr("Set Frame Advance Delay"));
    connect(act, SIGNAL(triggered()), this, SLOT(emuSetFrameAdvDelay(void)) );

    subMenu->addAction(act);

	 //-----------------------------------------------------------------------
	 // Tools
    toolsMenu = menuBar()->addMenu(tr("Tools"));

	 // Tools -> Cheats
	 cheatsAct = new QAction(tr("Cheats..."), this);
    //cheatsAct->setShortcut( QKeySequence(tr("Shift+F7")));
    cheatsAct->setStatusTip(tr("Open Cheat Window"));
    connect(cheatsAct, SIGNAL(triggered()), this, SLOT(openCheats(void)) );

    toolsMenu->addAction(cheatsAct);

	 // Tools -> RAM Search
	 ramSearchAct = new QAction(tr("RAM Search..."), this);
    //ramSearchAct->setShortcut( QKeySequence(tr("Shift+F7")));
    ramSearchAct->setStatusTip(tr("Open RAM Search Window"));
    connect(ramSearchAct, SIGNAL(triggered()), this, SLOT(openRamSearch(void)) );

    toolsMenu->addAction(ramSearchAct);

	 // Tools -> RAM Watch
	 ramWatchAct = new QAction(tr("RAM Watch..."), this);
    //ramWatchAct->setShortcut( QKeySequence(tr("Shift+F7")));
    ramWatchAct->setStatusTip(tr("Open RAM Watch Window"));
    connect(ramWatchAct, SIGNAL(triggered()), this, SLOT(openRamWatch(void)) );

    toolsMenu->addAction(ramWatchAct);

	 //-----------------------------------------------------------------------
	 // Debug
    debugMenu = menuBar()->addMenu(tr("Debug"));

	 // Debug -> Debugger 
	 debuggerAct = new QAction(tr("Debugger..."), this);
    //debuggerAct->setShortcut( QKeySequence(tr("Shift+F7")));
    debuggerAct->setStatusTip(tr("Open 6502 Debugger"));
    connect(debuggerAct, SIGNAL(triggered()), this, SLOT(openDebugWindow(void)) );

    debugMenu->addAction(debuggerAct);

	 // Debug -> Hex Editor
	 hexEditAct = new QAction(tr("Hex Editor..."), this);
    //hexEditAct->setShortcut( QKeySequence(tr("Shift+F7")));
    hexEditAct->setStatusTip(tr("Open Memory Hex Editor"));
    connect(hexEditAct, SIGNAL(triggered()), this, SLOT(openHexEditor(void)) );

    debugMenu->addAction(hexEditAct);

	 // Debug -> PPU Viewer
	 ppuViewAct = new QAction(tr("PPU Viewer..."), this);
    //ppuViewAct->setShortcut( QKeySequence(tr("Shift+F7")));
    ppuViewAct->setStatusTip(tr("Open PPU Viewer"));
    connect(ppuViewAct, SIGNAL(triggered()), this, SLOT(openPPUViewer(void)) );

    debugMenu->addAction(ppuViewAct);

	 // Debug -> Name Table Viewer
	 ntViewAct = new QAction(tr("Name Table Viewer..."), this);
    //ntViewAct->setShortcut( QKeySequence(tr("Shift+F7")));
    ntViewAct->setStatusTip(tr("Open Name Table Viewer"));
    connect(ntViewAct, SIGNAL(triggered()), this, SLOT(openNTViewer(void)) );

    debugMenu->addAction(ntViewAct);

	 // Debug -> Trace Logger
	 traceLogAct = new QAction(tr("Trace Logger..."), this);
    //traceLogAct->setShortcut( QKeySequence(tr("Shift+F7")));
    traceLogAct->setStatusTip(tr("Open Trace Logger"));
    connect(traceLogAct, SIGNAL(triggered()), this, SLOT(openTraceLogger(void)) );

    debugMenu->addAction(traceLogAct);

	 // Debug -> Code/Data Logger
	 codeDataLogAct = new QAction(tr("Code/Data Logger..."), this);
    //codeDataLogAct->setShortcut( QKeySequence(tr("Shift+F7")));
    codeDataLogAct->setStatusTip(tr("Open Code Data Logger"));
    connect(codeDataLogAct, SIGNAL(triggered()), this, SLOT(openCodeDataLogger(void)) );

    debugMenu->addAction(codeDataLogAct);

	 // Debug -> Game Genie Encode/Decode Viewer
	 ggEncodeAct = new QAction(tr("Game Genie Encode/Decode"), this);
    //ggEncodeAct->setShortcut( QKeySequence(tr("Shift+F7")));
    ggEncodeAct->setStatusTip(tr("Open Game Genie Encode/Decode"));
    connect(ggEncodeAct, SIGNAL(triggered()), this, SLOT(openGGEncoder(void)) );

    debugMenu->addAction(ggEncodeAct);

	 // Debug -> iNES Header Editor
	 iNesEditAct = new QAction(tr("iNES Header Editor..."), this);
    //iNesEditAct->setShortcut( QKeySequence(tr("Shift+F7")));
    iNesEditAct->setStatusTip(tr("Open iNES Header Editor"));
    connect(iNesEditAct, SIGNAL(triggered()), this, SLOT(openNesHeaderEditor(void)) );

    debugMenu->addAction(iNesEditAct);

	 //-----------------------------------------------------------------------
	 // Movie
    movieMenu = menuBar()->addMenu(tr("Movie"));

	 // Movie -> Play
	 openMovAct = new QAction(tr("Play"), this);
    openMovAct->setShortcut( QKeySequence(tr("Shift+F7")));
    openMovAct->setStatusTip(tr("Play Movie File"));
    connect(openMovAct, SIGNAL(triggered()), this, SLOT(openMovie(void)) );

    movieMenu->addAction(openMovAct);

	 // Movie -> Stop
	 stopMovAct = new QAction(tr("Stop"), this);
    //stopMovAct->setShortcut( QKeySequence(tr("Shift+F7")));
    stopMovAct->setStatusTip(tr("Stop Movie Recording"));
    connect(stopMovAct, SIGNAL(triggered()), this, SLOT(stopMovie(void)) );

    movieMenu->addAction(stopMovAct);

    movieMenu->addSeparator();

	 // Movie -> Record
	 recMovAct = new QAction(tr("Record"), this);
    recMovAct->setShortcut( QKeySequence(tr("Shift+F5")));
    recMovAct->setStatusTip(tr("Record Movie"));
    connect(recMovAct, SIGNAL(triggered()), this, SLOT(recordMovie(void)) );

    movieMenu->addAction(recMovAct);

	  // Movie -> Record As
	 recAsMovAct = new QAction(tr("Record As"), this);
    //recAsMovAct->setShortcut( QKeySequence(tr("Shift+F5")));
    recAsMovAct->setStatusTip(tr("Record Movie"));
    connect(recAsMovAct, SIGNAL(triggered()), this, SLOT(recordMovieAs(void)) );

    movieMenu->addAction(recAsMovAct);

	 //-----------------------------------------------------------------------
	 // Help
    helpMenu = menuBar()->addMenu(tr("Help"));
 
	 // Help -> About FCEUX
	 aboutAct = new QAction(tr("About FCEUX"), this);
    aboutAct->setStatusTip(tr("About FCEUX"));
    connect(aboutAct, SIGNAL(triggered()), this, SLOT(aboutFCEUX(void)) );

    helpMenu->addAction(aboutAct);

	 // Help -> About Qt
	 aboutActQt = new QAction(tr("About Qt"), this);
    aboutActQt->setStatusTip(tr("About Qt"));
    connect(aboutActQt, SIGNAL(triggered()), this, SLOT(aboutQt(void)) );

    helpMenu->addAction(aboutActQt);
};
//---------------------------------------------------------------------------
void consoleWin_t::closeApp(void)
{
	nes_shm->runEmulator = 0;

	emulatorThread->quit();
	emulatorThread->wait( 1000 );

	fceuWrapperLock();
	fceuWrapperClose();
	fceuWrapperUnLock();

	// LoadGame() checks for an IP and if it finds one begins a network session
	// clear the NetworkIP field so this doesn't happen unintentionally
	g_config->setOption ("SDL.NetworkIP", "");
	g_config->save ();

	//qApp::quit();
	qApp->quit();
}
//---------------------------------------------------------------------------
int  consoleWin_t::showListSelectDialog( const char *title, std::vector <std::string> &l )
{
	if ( QThread::currentThread() == emulatorThread )
	{
		printf("Cannot display list selection dialog from within emulation thread...\n");
		return 0;
	}
	int ret, idx = 0;
	QDialog dialog(this);
	QVBoxLayout *mainLayout;
	QHBoxLayout *hbox;
	QPushButton *okButton, *cancelButton;
	QTreeWidget *tree;
	QTreeWidgetItem *item;

	dialog.setWindowTitle( tr(title) );

	tree = new QTreeWidget();

	tree->setColumnCount(1);

	item = new QTreeWidgetItem();
	item->setText( 0, QString::fromStdString( "File" ) );
	item->setTextAlignment( 0, Qt::AlignLeft);

	tree->setHeaderItem( item );

	tree->header()->setSectionResizeMode( QHeaderView::ResizeToContents );

	for (size_t i=0; i<l.size(); i++)
	{
		item = new QTreeWidgetItem();

		item->setText( 0, QString::fromStdString( l[i] ) );

		item->setTextAlignment( 0, Qt::AlignLeft);

		tree->addTopLevelItem( item );
	}

	mainLayout = new QVBoxLayout();

	hbox         = new QHBoxLayout();
	okButton     = new QPushButton( tr("OK") );
	cancelButton = new QPushButton( tr("Cancel") );

	mainLayout->addWidget( tree );
	mainLayout->addLayout( hbox );
	hbox->addWidget( cancelButton );
	hbox->addWidget(     okButton );

	connect(     okButton, SIGNAL(clicked(void)), &dialog, SLOT(accept(void)) );
	connect( cancelButton, SIGNAL(clicked(void)), &dialog, SLOT(reject(void)) );

	dialog.setLayout( mainLayout );

	ret = dialog.exec();

	if ( ret == QDialog::Accepted )
	{
		idx = 0;

		item = tree->currentItem();

	   if ( item != NULL )
	   {
			idx = tree->indexOfTopLevelItem(item);
		}
	}
	else
	{
		idx = -1;
	}
	return idx;
}
//---------------------------------------------------------------------------

void consoleWin_t::openROMFile(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	std::string last;
	char dir[512];
	QFileDialog  dialog(this, tr("Open ROM File") );

	const QStringList filters(
			{ "All Useable files (*.nes *.NES *.nsf *.NSF *.fds *.FDS *.unf *.UNF *.unif *.UNIF *.zip *.ZIP)",
           "NES files (*.nes *.NES)",
           "NSF files (*.nsf *.NSF)",
           "UNF files (*.unf *.UNF *.unif *.UNIF)",
           "FDS files (*.fds *.FDS)",
           "Any files (*)"
         });

	dialog.setFileMode(QFileDialog::ExistingFile);

	dialog.setNameFilters( filters );

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Open") );

	g_config->getOption ("SDL.LastOpenFile", &last );

	getDirFromFile( last.c_str(), dir );

	dialog.setDirectory( tr(dir) );

	// Check config option to use native file dialog or not
	g_config->getOption ("SDL.UseNativeFileDialog", &useNativeFileDialogVal);

	dialog.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogVal);

	dialog.show();
	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

   if ( filename.isNull() )
   {
      return;
   }
	qDebug() << "selected file path : " << filename.toUtf8();

	g_config->setOption ("SDL.LastOpenFile", filename.toStdString().c_str() );

	fceuWrapperLock();
	CloseGame ();
	LoadGame ( filename.toStdString().c_str() );
	fceuWrapperUnLock();

   return;
}

void consoleWin_t::closeROMCB(void)
{
	fceuWrapperLock();
	CloseGame();
	fceuWrapperUnLock();
}

void consoleWin_t::loadNSF(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	std::string last;
	char dir[512];
	QFileDialog  dialog(this, tr("Load NSF File") );

	dialog.setFileMode(QFileDialog::ExistingFile);

	dialog.setNameFilter(tr("NSF Sound Files (*.nsf *.NSF) ;; Zip Files (*.zip *.ZIP) ;; All files (*)"));

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Load") );

	g_config->getOption ("SDL.LastOpenNSF", &last );

	getDirFromFile( last.c_str(), dir );

	dialog.setDirectory( tr(dir) );

	// Check config option to use native file dialog or not
	g_config->getOption ("SDL.UseNativeFileDialog", &useNativeFileDialogVal);

	dialog.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogVal);

	dialog.show();
	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

	if ( filename.isNull() )
   {
      return;
   }
	qDebug() << "selected file path : " << filename.toUtf8();

	g_config->setOption ("SDL.LastOpenNSF", filename.toStdString().c_str() );

	fceuWrapperLock();
	LoadGame( filename.toStdString().c_str() );
	fceuWrapperUnLock();
}

void consoleWin_t::loadStateFrom(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	std::string last;
	char dir[512];
	QFileDialog  dialog(this, tr("Load State From File") );

	dialog.setFileMode(QFileDialog::ExistingFile);

	dialog.setNameFilter(tr("FCS & SAV Files (*.sav *.SAV *.fc? *.FC?) ;; All files (*)"));

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Load") );

	g_config->getOption ("SDL.LastLoadStateFrom", &last );

	getDirFromFile( last.c_str(), dir );

	dialog.setDirectory( tr(dir) );

	// Check config option to use native file dialog or not
	g_config->getOption ("SDL.UseNativeFileDialog", &useNativeFileDialogVal);

	dialog.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogVal);

	dialog.show();
	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

	if ( filename.isNull() )
   {
      return;
   }
	qDebug() << "selected file path : " << filename.toUtf8();

	g_config->setOption ("SDL.LastLoadStateFrom", filename.toStdString().c_str() );

	fceuWrapperLock();
	FCEUI_LoadState( filename.toStdString().c_str() );
	fceuWrapperUnLock();
}

void consoleWin_t::saveStateAs(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	std::string last;
	char dir[512];
	QFileDialog  dialog(this, tr("Save State To File") );

	dialog.setFileMode(QFileDialog::AnyFile);

	dialog.setNameFilter(tr("SAV Files (*.sav *.SAV) ;; All files (*)"));

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Save") );
	dialog.setDefaultSuffix( tr(".sav") );

	g_config->getOption ("SDL.LastSaveStateAs", &last );

	getDirFromFile( last.c_str(), dir );

	dialog.setDirectory( tr(dir) );

	// Check config option to use native file dialog or not
	g_config->getOption ("SDL.UseNativeFileDialog", &useNativeFileDialogVal);

	dialog.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogVal);

	dialog.show();
	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

	if ( filename.isNull() )
   {
      return;
   }
	qDebug() << "selected file path : " << filename.toUtf8();

	g_config->setOption ("SDL.LastSaveStateAs", filename.toStdString().c_str() );

	fceuWrapperLock();
	FCEUI_SaveState( filename.toStdString().c_str() );
	fceuWrapperUnLock();
}

void consoleWin_t::quickLoad(void)
{
	fceuWrapperLock();
	FCEUI_LoadState( NULL );
	fceuWrapperUnLock();
}

void consoleWin_t::quickSave(void)
{
	fceuWrapperLock();
	FCEUI_SaveState( NULL );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState0(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 0, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState1(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 1, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState2(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 2, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState3(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 3, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState4(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 4, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState5(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 5, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState6(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 6, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState7(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 7, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState8(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 8, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::changeState9(void)
{
	fceuWrapperLock();
	FCEUI_SelectState( 9, 1 );
	fceuWrapperUnLock();
}

void consoleWin_t::takeScreenShot(void)
{
	fceuWrapperLock();
	FCEUI_SaveSnapshot();
	fceuWrapperUnLock();
}

void consoleWin_t::loadLua(void)
{
#ifdef _S9XLUA_H
	LuaControlDialog_t *luaCtrlWin;

	//printf("Open Lua Control Window\n");
	
   luaCtrlWin = new LuaControlDialog_t(this);
	
   luaCtrlWin->show();
#endif
}

void consoleWin_t::openGamePadConfWin(void)
{
	//printf("Open GamePad Config Window\n");
	
	openGamePadConfWindow(this);
}

void consoleWin_t::openGameSndConfWin(void)
{
	ConsoleSndConfDialog_t *sndConfWin;

	//printf("Open Sound Config Window\n");
	
   sndConfWin = new ConsoleSndConfDialog_t(this);
	
   sndConfWin->show();
}

void consoleWin_t::openGameVideoConfWin(void)
{
	ConsoleVideoConfDialog_t *vidConfWin;

	//printf("Open Video Config Window\n");
	
   vidConfWin = new ConsoleVideoConfDialog_t(this);
	
   vidConfWin->show();
}

void consoleWin_t::openHotkeyConfWin(void)
{
	HotKeyConfDialog_t *hkConfWin;

	//printf("Open Hot Key Config Window\n");
	
   hkConfWin = new HotKeyConfDialog_t(this);
	
   hkConfWin->show();
}

void consoleWin_t::openPaletteConfWin(void)
{
	PaletteConfDialog_t *paletteConfWin;

	//printf("Open Palette Config Window\n");
	
   paletteConfWin = new PaletteConfDialog_t(this);
	
   paletteConfWin->show();
}

void consoleWin_t::openGuiConfWin(void)
{
	GuiConfDialog_t *guiConfWin;

	//printf("Open GUI Config Window\n");
	
   guiConfWin = new GuiConfDialog_t(this);
	
   guiConfWin->show();
}

void consoleWin_t::openMovieOptWin(void)
{
	MovieOptionsDialog_t *win;

	//printf("Open Movie Options Window\n");
	
   win = new MovieOptionsDialog_t(this);
	
   win->show();
}

void consoleWin_t::openCheats(void)
{
	//printf("Open GUI Cheat Window\n");
	
   openCheatDialog(this);
}

void consoleWin_t::openRamWatch(void)
{
	RamWatchDialog_t *ramWatchWin;

	//printf("Open GUI RAM Watch Window\n");
	
   ramWatchWin = new RamWatchDialog_t(this);
	
   ramWatchWin->show();
}

void consoleWin_t::openRamSearch(void)
{
	//printf("Open GUI RAM Search Window\n");
	openRamSearchWindow(this);
}

void consoleWin_t::openDebugWindow(void)
{
	ConsoleDebugger *debugWin;

	//printf("Open GUI 6502 Debugger Window\n");
	
   debugWin = new ConsoleDebugger(this);
	
   debugWin->show();
}

void consoleWin_t::openHexEditor(void)
{
	HexEditorDialog_t *hexEditWin;

	//printf("Open GUI Hex Editor Window\n");
	
   hexEditWin = new HexEditorDialog_t(this);
	
   hexEditWin->show();
}

void consoleWin_t::openPPUViewer(void)
{
	//printf("Open GUI PPU Viewer Window\n");
	
	openPPUViewWindow(this);
}

void consoleWin_t::openNTViewer(void)
{
	//printf("Open GUI Name Table Viewer Window\n");
	
	openNameTableViewWindow(this);
}

void consoleWin_t::openCodeDataLogger(void)
{
	CodeDataLoggerDialog_t *cdlWin;

	//printf("Open Code Data Logger Window\n");
	
   cdlWin = new CodeDataLoggerDialog_t(this);
	
   cdlWin->show();
}

void consoleWin_t::openGGEncoder(void)
{
	GameGenieDialog_t *win;

	//printf("Open Game Genie Window\n");
	
   win = new GameGenieDialog_t(this);
	
   win->show();
}

void consoleWin_t::openNesHeaderEditor(void)
{
	iNesHeaderEditor_t *win;

	//printf("Open iNES Header Editor Window\n");
	
   win = new iNesHeaderEditor_t(this);
	
	if ( win->isInitialized() )
	{
		win->show();
	}
	else
	{
		delete win;
	}
}

void consoleWin_t::openTraceLogger(void)
{
	openTraceLoggerWindow(this);
}

void consoleWin_t::toggleAutoResume(void)
{
   //printf("Auto Resume: %i\n", autoResume->isChecked() );

	g_config->setOption ("SDL.AutoResume", (int) autoResume->isChecked() );

	AutoResumePlay = autoResume->isChecked();
}

void consoleWin_t::toggleFullscreen(void)
{
	if ( isFullScreen() )
	{
		showNormal();
	}
	else
	{
		showFullScreen();
	}
}

void consoleWin_t::powerConsoleCB(void)
{
	fceuWrapperLock();
	FCEUI_PowerNES();
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::consoleHardReset(void)
{
	fceuWrapperLock();
	fceuWrapperHardReset();
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::consoleSoftReset(void)
{
	fceuWrapperLock();
	fceuWrapperSoftReset();
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::consolePause(void)
{
	fceuWrapperLock();
	fceuWrapperTogglePause();
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::toggleGameGenie(bool checked)
{
	int gg_enabled;

	fceuWrapperLock();
	g_config->getOption ("SDL.GameGenie", &gg_enabled);
	g_config->setOption ("SDL.GameGenie", !gg_enabled);
	g_config->save ();
	FCEUI_SetGameGenie (gg_enabled);
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::loadGameGenieROM(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	std::string last;
	char dir[512];
	QFileDialog  dialog(this, tr("Open Game Genie ROM") );

	dialog.setFileMode(QFileDialog::ExistingFile);

	dialog.setNameFilter(tr("GG ROM File (gg.rom  *Genie*.nes) ;; All files (*)"));

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Load") );

	g_config->getOption ("SDL.LastOpenFile", &last );

	getDirFromFile( last.c_str(), dir );

	dialog.setDirectory( tr(dir) );

	// Check config option to use native file dialog or not
	g_config->getOption ("SDL.UseNativeFileDialog", &useNativeFileDialogVal);

	dialog.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogVal);

	dialog.show();
	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

   if ( filename.isNull() )
   {
      return;
   }
	qDebug() << "selected file path : " << filename.toUtf8();

	g_config->setOption ("SDL.LastOpenFile", filename.toStdString().c_str() );

	// copy file to proper place (~/.fceux/gg.rom)
	std::ifstream f1 ( filename.toStdString().c_str(), std::fstream::binary);
	std::string fn_out = FCEU_MakeFName (FCEUMKF_GGROM, 0, "");
	std::ofstream f2 (fn_out.c_str (),
	std::fstream::trunc | std::fstream::binary);
	f2 << f1.rdbuf ();

   return;
}

void consoleWin_t::insertCoin(void)
{
	fceuWrapperLock();
	FCEUI_VSUniCoin();
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::fdsSwitchDisk(void)
{
	fceuWrapperLock();
	FCEU_FDSSelect();
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::fdsEjectDisk(void)
{
	fceuWrapperLock();
	FCEU_FDSInsert();
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::fdsLoadBiosFile(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	std::string last;
	char dir[512];
	QFileDialog  dialog(this, tr("Load FDS BIOS (disksys.rom)") );

	dialog.setFileMode(QFileDialog::ExistingFile);

	dialog.setNameFilter(tr("ROM files (*.rom *.ROM) ;; All files (*)"));

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Load") );

	g_config->getOption ("SDL.LastOpenFile", &last );

	getDirFromFile( last.c_str(), dir );

	dialog.setDirectory( tr(dir) );

	// Check config option to use native file dialog or not
	g_config->getOption ("SDL.UseNativeFileDialog", &useNativeFileDialogVal);

	dialog.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogVal);

	dialog.show();
	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

   if ( filename.isNull() )
   {
      return;
   }
	qDebug() << "selected file path : " << filename.toUtf8();

	// copy BIOS file to proper place (~/.fceux/disksys.rom)
	std::ifstream fdsBios (filename.toStdString().c_str(), std::fstream::binary);
	std::string output_filename =
		FCEU_MakeFName (FCEUMKF_FDSROM, 0, "");
	std::ofstream outFile (output_filename.c_str (),
			       std::fstream::trunc | std::fstream::
			       binary);
	outFile << fdsBios.rdbuf ();
	if (outFile.fail ())
	{
		FCEUD_PrintError ("Error copying the FDS BIOS file.");
	}
	else
	{
		printf("Famicom Disk System BIOS loaded.  If you are you having issues, make sure your BIOS file is 8KB in size.\n");
	}

   return;
}

void consoleWin_t::emuSpeedUp(void)
{
   IncreaseEmulationSpeed();
}

void consoleWin_t::emuSlowDown(void)
{
   DecreaseEmulationSpeed();
}

void consoleWin_t::emuSlowestSpd(void)
{
   FCEUD_SetEmulationSpeed( EMUSPEED_SLOWEST );
}

void consoleWin_t::emuNormalSpd(void)
{
   FCEUD_SetEmulationSpeed( EMUSPEED_NORMAL );
}

void consoleWin_t::emuFastestSpd(void)
{
   FCEUD_SetEmulationSpeed( EMUSPEED_FASTEST );
}

void consoleWin_t::emuCustomSpd(void)
{
	int ret;
	QInputDialog dialog(this);

   dialog.setWindowTitle( tr("Emulation Speed") );
   dialog.setLabelText( tr("Enter a percentage from 1 to 1000.") );
   dialog.setOkButtonText( tr("Ok") );
   dialog.setInputMode( QInputDialog::IntInput );
   dialog.setIntRange( 1, 1000 );
   dialog.setIntValue( 100 );

   dialog.show();
   ret = dialog.exec();

   if ( QDialog::Accepted == ret )
   {
      int spdPercent;

      spdPercent = dialog.intValue();

      CustomEmulationSpeed( spdPercent );
   }
}

void consoleWin_t::emuSetFrameAdvDelay(void)
{
	int ret;
	QInputDialog dialog(this);

   dialog.setWindowTitle( tr("Frame Advance Delay") );
   dialog.setLabelText( tr("How much time should elapse before holding the frame advance unpauses the simulation?") );
   dialog.setOkButtonText( tr("Ok") );
   dialog.setInputMode( QInputDialog::IntInput );
   dialog.setIntRange( 0, 1000 );
   dialog.setIntValue( frameAdvance_Delay );

   dialog.show();
   ret = dialog.exec();

   if ( QDialog::Accepted == ret )
   {
      frameAdvance_Delay = dialog.intValue();
   }
}

void consoleWin_t::openMovie(void)
{
	MoviePlayDialog_t *win;

	win = new MoviePlayDialog_t(this);

	win->show();
}

void consoleWin_t::stopMovie(void)
{
	fceuWrapperLock();
	FCEUI_StopMovie();
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::recordMovie(void)
{
	fceuWrapperLock();
	if (fceuWrapperGameLoaded())
	{
		std::string name = FCEU_MakeFName (FCEUMKF_MOVIE, 0, 0);
		FCEUI_printf ("Recording movie to %s\n", name.c_str ());
		FCEUI_SaveMovie (name.c_str (), MOVIE_FLAG_NONE, L"");
	}
	fceuWrapperUnLock();
   return;
}

void consoleWin_t::recordMovieAs(void)
{
	int ret, useNativeFileDialogVal;
	QString filename;
	std::string last;
	char dir[512];
	QFileDialog  dialog(this, tr("Save FM2 Movie for Recording") );

	dialog.setFileMode(QFileDialog::AnyFile);

	dialog.setNameFilter(tr("FM2 Movies (*.fm2) ;; All files (*)"));

	dialog.setViewMode(QFileDialog::List);
	dialog.setFilter( QDir::AllEntries | QDir::AllDirs | QDir::Hidden );
	dialog.setLabelText( QFileDialog::Accept, tr("Save") );

	g_config->getOption ("SDL.LastOpenMovie", &last );

	getDirFromFile( last.c_str(), dir );

	dialog.setDirectory( tr(dir) );

	// Check config option to use native file dialog or not
	g_config->getOption ("SDL.UseNativeFileDialog", &useNativeFileDialogVal);

	dialog.setOption(QFileDialog::DontUseNativeDialog, !useNativeFileDialogVal);

	dialog.show();
	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

   if ( filename.isNull() )
   {
      return;
   }
	qDebug() << "selected file path : " << filename.toUtf8();

	int pauseframe;
	g_config->getOption ("SDL.PauseFrame", &pauseframe);
	g_config->setOption ("SDL.PauseFrame", 0);

	FCEUI_printf ("Recording movie to %s\n", filename.toStdString().c_str() );

	fceuWrapperLock();
	std::string s = GetUserText ("Author name");
	std::wstring author (s.begin (), s.end ());

	FCEUI_SaveMovie ( filename.toStdString().c_str(), MOVIE_FLAG_NONE, author);
	fceuWrapperUnLock();

   return;
}

void consoleWin_t::aboutFCEUX(void)
{
	AboutWindow *aboutWin;

	//printf("About FCEUX Window\n");
	
   aboutWin = new AboutWindow(this);
	
   aboutWin->show();
   return;
}

void consoleWin_t::aboutQt(void)
{
	//printf("About Qt Window\n");
	
	QMessageBox::aboutQt(this);

   //printf("About Qt Destroyed\n");
   return;
}

void consoleWin_t::syncActionConfig( QAction *act, const char *property )
{
	if ( act->isCheckable() )
	{
		int enable;
		g_config->getOption ( property, &enable);

		act->setChecked( enable ? true : false );
	}
}

void consoleWin_t::updatePeriodic(void)
{
	//struct timespec ts;
	//double t;

	//clock_gettime( CLOCK_REALTIME, &ts );

	//t = (double)ts.tv_sec + (double)(ts.tv_nsec * 1.0e-9);
   //printf("Run Frame %f\n", t);
	
	// Update Input Devices
	FCEUD_UpdateInput();
	
	// RePaint Game Viewport
	if ( nes_shm->blitUpdated )
	{
		nes_shm->blitUpdated = 0;

		if ( viewport_SDL )
		{
			viewport_SDL->transfer2LocalBuffer();
			viewport_SDL->render();
		}
		else
		{
			viewport_GL->transfer2LocalBuffer();
			//viewport_GL->repaint();
			viewport_GL->update();
		}
	}

	if ( errorMsgValid )
	{
		showErrorMsgWindow();
		errorMsgValid = false;
	}

   return;
}

void emulatorThread_t::run(void)
{
	printf("Emulator Start\n");
	nes_shm->runEmulator = 1;

	while ( nes_shm->runEmulator )
	{
		fceuWrapperUpdate();
	}
	printf("Emulator Exit\n");
	emit finished();
}
