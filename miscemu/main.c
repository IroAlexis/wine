/*
 * Emulator initialisation code
 *
 */

#include <stdlib.h>
#include <assert.h>
#include "wine/winbase16.h"
#include "callback.h"
#include "debugger.h"
#include "main.h"
#include "miscemu.h"
#include "module.h"
#include "options.h"
#include "process.h"
#include "thread.h"
#include "task.h"
#include "stackframe.h"
#include "wine/exception.h"
#include "debugtools.h"

static int MAIN_argc;
static char **MAIN_argv;


/***********************************************************************
 *           Main loop of initial task
 */
void MAIN_EmulatorRun( void )
{
    char startProg[256], defProg[256];
    HINSTANCE handle;
    int i, tasks = 0;
    MSG msg;

    /* Load system DLLs into the initial process (and initialize them) */
    if (   !LoadLibrary16("GDI.EXE" ) || !LoadLibraryA("GDI32.DLL" )
        || !LoadLibrary16("USER.EXE") || !LoadLibraryA("USER32.DLL"))
        ExitProcess( 1 );

    /* Get pointers to USER routines called by KERNEL */
    THUNK_InitCallout();

    /* Call FinalUserInit routine */
    Callout.FinalUserInit16();

    /* Call InitApp for initial task */
    Callout.InitApp16( MapHModuleLS( 0 ) );

    /* Add the Default Program if no program on the command line */
    if (!MAIN_argv[1])
    {
        PROFILE_GetWineIniString( "programs", "Default", "",
                                  defProg, sizeof(defProg) );
        if (defProg[0]) MAIN_argv[MAIN_argc++] = defProg;
    }
    
    /* Add the Startup Program to the run list */
    PROFILE_GetWineIniString( "programs", "Startup", "", 
			       startProg, sizeof(startProg) );
    if (startProg[0]) MAIN_argv[MAIN_argc++] = startProg;

    /* Abort if no executable on command line */
    if (MAIN_argc <= 1) 
    {
    	MAIN_Usage(MAIN_argv[0]);
        ExitProcess( 1 );
    }

    /* Load and run executables given on command line */
    for (i = 1; i < MAIN_argc; i++)
    {
        if ((handle = WinExec( MAIN_argv[i], SW_SHOWNORMAL )) < 32)
        {
            MESSAGE("wine: can't exec '%s': ", MAIN_argv[i]);
            switch (handle)
            {
            case 2: MESSAGE("file not found\n" ); break;
            case 11: MESSAGE("invalid exe file\n" ); break;
            default: MESSAGE("error=%d\n", handle ); break;
            }
        }
        else tasks++;
    }

    if (!tasks)
    {
        MESSAGE("wine: no executable file found.\n" );
        ExitProcess( 0 );
    }

    /* Start message loop for desktop window */

    while ( GetNumTasks16() > 1  && Callout.GetMessageA( &msg, 0, 0, 0 ) )
    {
        Callout.TranslateMessage( &msg );
        Callout.DispatchMessageA( &msg );
    }

    ExitProcess( 0 );
}


/**********************************************************************
 *           main
 */
int main( int argc, char *argv[] )
{
    NE_MODULE *pModule;
    extern char * DEBUG_argv0;

    /*
     * Save this so that the internal debugger can get a hold of it if
     * it needs to.
     */
    DEBUG_argv0 = argv[0];

    /* Create the initial process */
    if (!PROCESS_Init()) return FALSE;

    /* Parse command-line */
    if (!MAIN_WineInit( &argc, argv )) return 1;
    MAIN_argc = argc; MAIN_argv = argv;

    /* Set up debugger hook */
    EXC_SetDebugEventHook( wine_debugger );

    if (Options.debug) 
        TASK_AddTaskEntryBreakpoint = DEBUG_AddTaskEntryBreakpoint;

    /* Initialize everything */
    if (!MAIN_MainInit()) return 1;

    /* Load kernel modules */
    if (!LoadLibrary16( "KRNL386.EXE" )) return 1;
    if (!LoadLibraryA( "KERNEL32" )) return 1;

    /* Create initial task */
    if ( !(pModule = NE_GetPtr( GetModuleHandle16( "KERNEL" ) )) ) return 1;
    if ( !TASK_Create( pModule, FALSE ) ) return 1;

    /* Switch to initial task */
    PostEvent16( PROCESS_Current()->task );
    TASK_Reschedule();

    /* Switch stacks and jump to MAIN_EmulatorRun */
    CALL32_Init( &IF1632_CallLargeStack, MAIN_EmulatorRun, NtCurrentTeb()->stack_top );

    MESSAGE( "main: Should never happen: returned from CALL32_Init()\n" );
    return 0;
}
