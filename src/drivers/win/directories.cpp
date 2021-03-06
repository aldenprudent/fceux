#include "../../version.h"
#include "common.h"
#include "main.h"
#include "window.h"
#include "gui.h"

///Processes information from the Directories selection dialog after the dialog was closed.
///@param hwndDlg Handle of the dialog window.
void CloseDirectoriesDialog(HWND hwndDlg)
{
	// Update the information from the screenshot naming checkbox
	
	// this variable isn't used at all, snap is always name-based
	//if(IsDlgButtonChecked(hwndDlg, CHECK_SCREENSHOT_NAMES) == BST_CHECKED)
	//{
	//	eoptions |= EO_SNAPNAME;
	//}
	//else
	//{
	//	eoptions &= ~EO_SNAPNAME;
	//}

	RemoveDirs();   // Remove empty directories.

	// Read the information from the edit fields and update the
	// necessary variables.
	for(unsigned int curr_dir = 0; curr_dir < NUMBER_OF_DIRECTORIES; curr_dir++)
	{
		LONG len;
		len = SendDlgItemMessage(hwndDlg, edit_id[curr_dir], WM_GETTEXTLENGTH, 0, 0);

		if(len <= 0)
		{
			if(directory_names[curr_dir])
			{
				free(directory_names[curr_dir]);
			}

			directory_names[curr_dir] = 0;
			continue;
		}

		len++; // Add 1 for null character.

		if( !(directory_names[curr_dir] = (char*)malloc(len))) //mbg merge 7/17/06 added cast
		{
			continue;
		}

		if(!GetDlgItemText(hwndDlg, edit_id[curr_dir], directory_names[curr_dir], len))
		{
			free(directory_names[curr_dir]);
			directory_names[curr_dir] = 0;
			continue;
		}

		if (!directoryExists(directory_names[curr_dir]))
		{
			const char* mask = "Error: Directory %s does not exist. Create this directory?";

			char* buffer = (char*)malloc(strlen(mask) + strlen(directory_names[curr_dir]) + 1);

			sprintf(buffer, mask, directory_names[curr_dir]);

			if ( MessageBox(hwndDlg, buffer, FCEU_NAME, MB_ICONERROR | MB_YESNO) == IDYES )
			{
				if (!CreateDirectory(directory_names[curr_dir], 0))
				{
					MessageBox(hwndDlg, "Error: Couldn't create directory. Please choose a different directory.", FCEU_NAME, MB_ICONERROR | MB_OK);
					free(buffer);
					return;
				}
			}
			else
			{
				free(buffer);
				return;
			}

			free(buffer);
		}

	}
	//initDirectories();	//adelikat 03/02/09 - commenting out.  This function fills in empty directory overrides, which is not what we want, we want the user to have the option of leaving them blank
	CreateDirs();   // Create needed directories.
	SetDirs();      // Set the directories in the core.

	EndDialog(hwndDlg, 0);
}

///Callback function for the directories configuration dialog.
static INT_PTR CALLBACK DirConCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
		case WM_INITDIALOG:

			// Initialize the directories textboxes
			for(unsigned int curr_dir = 0; curr_dir < NUMBER_OF_DIRECTORIES; curr_dir++)
			{
				SetDlgItemText(hwndDlg, edit_id[curr_dir], directory_names[curr_dir]);
			}

			// Check the screenshot naming checkbox if necessary
			// this variable isn't used at all, snap is always name-based
			//if(eoptions & EO_SNAPNAME)
			//{
			//	CheckDlgButton(hwndDlg, CHECK_SCREENSHOT_NAMES, BST_CHECKED);
			//}

			CenterWindowOnScreen(hwndDlg);

			break;

		case WM_CLOSE:
		case WM_QUIT:
			EndDialog(hwndDlg, 0);
			break;

		case WM_COMMAND:
			switch (HIWORD(wParam))
			{
				case BN_CLICKED:
					switch(LOWORD(wParam))
					{
						case CLOSE_BUTTON:
							CloseDirectoriesDialog(hwndDlg);
							CloseDirectoriesDialog(hwndDlg);	//adelikat:  Hacky but this fixes the directory overides bug (or at least some instances of it).  This of course really just puts a band-aid on the real problem.
							break;
						default:
							static char *helpert[14] = {
								"Roms",
								"Battery Saves",
								"Save States",
								"FDS Bios Rom",
								"Screenshots",
								"Cheats",
								"Movies",
								"Memory Watch",
								"Basic Bot",
								"Macro files",
								"Input Presets",
								"Lua Scripts",
								"Avi output",
								"Base",
							};

							for (int i = 0; i < NUMBER_OF_DIRECTORIES; ++i)
							{
								if (browse_btn_id[i] == LOWORD(wParam))
								{
									// If a directory selection button was pressed, ask the
									// user for a directory.

									char name[MAX_PATH];
									char path[MAX_PATH];
									char caption[256];

									GetDlgItemText(hwndDlg, edit_id[i], path, MAX_PATH);
									sprintf(caption, "Select a directory for %s.", helpert[i]);
									if (BrowseForFolder(hwndDlg, caption, name, path))
										SetDlgItemText(hwndDlg, edit_id[i], name);
									break;
								}
							}
					}
			}

	}

	return 0;
}

//Shows the dialog for configuring the standard directories.
void ConfigDirectories()
{
	DialogBox(fceu_hInstance, "DIRCONFIG", hAppWnd, DirConCallB);
}

