//----------------------------------------------------------------------------
// Copyright( c ) 2015, Robert Kimball
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//----------------------------------------------------------------------------

#include <Windows.h>
#include <cassert>
#include <stdio.h>

#include "../osThread.h"
#include "../osMutex.h"
#include "../osEvent.h"

static const int  MAX_THREADS = 48;
static osThread*  Threads[ MAX_THREADS ];

static osMutex    Mutex( "Thread List" );
static DWORD      dwTlsIndex;
static osThread   MainThread;
static bool       IsInitialized = false;

typedef struct WinThreadParam 
{
   ThreadEntryPtr Entry;
   void*          Param;
   osThread*      Thread;
} *WINTHREADPARAMPTR;

osThread::osThread()
{
   USleepTime = 0;
   State = INIT;
   Filename = "";
   Linenumber = 0;
   Handle = 0;
   ThreadId = 0;
}

osThread::~osThread()
{
}

DWORD WINAPI WinThreadEntry( LPVOID param )
{
   WINTHREADPARAMPTR thread = (WINTHREADPARAMPTR)param;

   // Set the thread local storage to point to this osThread
   TlsSetValue( dwTlsIndex, thread->Thread );

   thread->Entry( thread->Param );

   for( int i = 0; i<MAX_THREADS; i++ )
   {
      if( Threads[ i ] == thread->Thread )
      {
         Threads[ i ] = NULL;
         break;
      }
   }

   return 0;
}

const char* osThread::GetName()
{
   return Name;
}

void* osThread::GetHandle()
{
   return Handle;
}

uint32_t osThread::GetThreadId()
{
   return ThreadId;
}

void osThread::Initialize()
{
   if( (dwTlsIndex = TlsAlloc()) == TLS_OUT_OF_INDEXES )
   {
      //ErrorExit( "TlsAlloc failed" );
   }

   // This is a hack to add the thread that called main() to the list
   printf( "setup main thread\n" );
   snprintf( MainThread.Name, NAME_LENGTH_MAX, "main" );
   TlsSetValue( dwTlsIndex, &MainThread );
   for( int i = 0; i<MAX_THREADS; i++ )
   {
      if( Threads[ i ] == NULL )
      {
         Threads[ i ] = &MainThread;
         break;
      }
   }
   IsInitialized = true;
}

int osThread::Create
( 
   ThreadEntryPtr entry, 
   char*          name, 
   int            stackSize, 
   int            priority, 
   void*          param 
)
{
   if( !IsInitialized )
   {
      Initialize();
   }
   printf( "osThread::Create( \"%s\" )\n", name );
   int               i;
   int               j;
   int               rc = 0;
   WINTHREADPARAMPTR threadParam;

   snprintf( Name, NAME_LENGTH_MAX, name );
   Priority = priority;

   // Allocate heap memory for thread parameter
   threadParam = 
      (WINTHREADPARAMPTR)HeapAlloc
      ( 
         GetProcessHeap(), 
         HEAP_ZERO_MEMORY, 
         sizeof( WinThreadParam ) 
       );

   // Set thread parameter values
   threadParam->Entry = entry;
   threadParam->Param = param;
   threadParam->Thread = this;

   DWORD tid;

   // Create thread
   Handle = CreateThread( NULL, 0, WinThreadEntry, threadParam, 0, &tid );
   ThreadId = tid;

   Mutex.Take( __FILE__, __LINE__ );
   for( i=0; i<MAX_THREADS; i++ )
   {
      if( Threads[ i ] == NULL )
      {
         Threads[ i ] = this;
         break;
      }
   }
   Mutex.Give();

   return rc;
}

int osThread::WaitForExit( int32_t millisecondWaitTimeout )
{
   if( millisecondWaitTimeout < 0 )
   {
      millisecondWaitTimeout = INFINITE;
   }
   return WaitForSingleObject( Handle, millisecondWaitTimeout );
}

void osThread::Sleep( unsigned long ms, const char* file, int line )
{
   osThread*   thread = GetCurrent();

   if( thread != 0 )
   {
      thread->Filename = file;
      thread->Linenumber = line;

      thread->USleepTime = ms * 1000;
      SleepEx( ms, true );
      thread->USleepTime = 0;
   }
   else
   {
      SleepEx( ms, true );
   }
}

void osThread::USleep( unsigned long us, const char* file, int line )
{
   osThread*   thread = GetCurrent();

   if( thread != 0 )
   {
      thread->Filename = file;
      thread->Linenumber = line;

      thread->USleepTime = us;
      SleepEx( us/1000, true );
      thread->USleepTime = 0;
   }
   else
   {
      SleepEx( us/1000, true );
   }
}

osThread* osThread::GetCurrent()
{
   return (osThread*)TlsGetValue( dwTlsIndex );
   //return GetThreadById( ::GetCurrentThreadId() );
}

int osThread::GetCurrentThreadId()
{
   return ::GetCurrentThreadId();
}

osThread* osThread::GetThreadById( int threadId )
{
   osThread* thread = 0;
   Mutex.Take();
   for( int i = 0; i < MAX_THREADS; i++ )
   {
      if( Threads[ i ] && Threads[ i ]->GetThreadId() == threadId )
      {
         //Found it
         thread = Threads[ i ];
         break;
      }
   }
   Mutex.Give();
   return thread;
}

void osThread::Kill()
{
   TerminateThread( Handle, 0 );
}

void osThread::SetState( THREAD_STATE state, const char* file, int line, void* obj )
{
   State = state;
   StateObject = obj;
   Filename = file;
   Linenumber = line;
}

void osThread::ClearState()
{
   State = RUNNING;
   StateObject = NULL;
   Filename = "";
   Linenumber = 0;
}

void osThread::Show( osPrintfInterface* pfunc )
{
   osMutex        *mutex;
   FILETIME       creationTime;
   FILETIME       exitTime;
   FILETIME       kernelTime;
   FILETIME       userTime;
   SYSTEMTIME     sysTime;
   HANDLE         handle;

   handle = GetCurrentProcess();
   pfunc->Printf( "Process Priority   = 0x%X\n", GetPriorityClass( handle ) );
   if( GetProcessTimes( handle, &creationTime, &exitTime, &kernelTime, &userTime ) )
   {
      FileTimeToSystemTime( &kernelTime, &sysTime );
      pfunc->Printf
      ( 
         "  Kernel Time      = %02d:%02d:%02d.%03d\n", 
         sysTime.wHour, 
         sysTime.wMinute, 
         sysTime.wSecond, 
         sysTime.wMilliseconds 
      );

      FileTimeToSystemTime( &userTime, &sysTime );
      pfunc->Printf
      ( 
         "  User Time        = %02d:%02d:%02d.%03d\n", 
         sysTime.wHour, 
         sysTime.wMinute, 
         sysTime.wSecond, 
         sysTime.wMilliseconds 
      );   
   }

   pfunc->Printf
   ( 
      "\n\n\"Reading\" means that the thread is reading a device or network socket\n\n"
   );
  
   pfunc->Printf( "----+--------------------+-------+--------------+--------------+--------------------------\n" );
   pfunc->Printf( "Pri |Thread Name         | ID    | Kernel Time  |  User Time   | State\n");
   pfunc->Printf( "----+--------------------+-------+--------------+--------------+--------------------------\n" );

   Mutex.Take( __FILE__, __LINE__ );

   for( int i=0; i<MAX_THREADS; i++ )
   {
      osThread* thread = Threads[ i ];
      if( !thread )
      {
         continue;
      }
      handle = thread->GetHandle();

      int priority;
      if( handle )
      {
         priority = GetThreadPriority( handle );
      }
      else
      {
         priority = -1;
      }
      GetThreadTimes( handle, &creationTime, &exitTime, &kernelTime, &userTime );

      pfunc->Printf
      ( 
         "%3d |%-20s|%6u | ",
         priority,
         thread->Name,
         handle
      );

      FileTimeToSystemTime( &kernelTime, &sysTime );
      pfunc->Printf( "%2d:%02d:%02d.%03d | ", sysTime.wHour, sysTime.wMinute, sysTime.wSecond, sysTime.wMilliseconds );

      FileTimeToSystemTime( &userTime, &sysTime );
      pfunc->Printf( "%2d:%02d:%02d.%03d | ", sysTime.wHour, sysTime.wMinute, sysTime.wSecond, sysTime.wMilliseconds );

      // Find out what this thread is doing
      switch( thread->State )
      {
      case INIT:
         pfunc->Printf( "init\n" );
         break;
      case RUNNING:
         pfunc->Printf( "running\n" );
         break;
      case PENDING_MUTEX:
         pfunc->Printf( "pending on mutex \"%s\"\n", ((osMutex*)(thread->StateObject))->GetName() );
         break;
      case PENDING_EVENT:
         pfunc->Printf( "pending event \"%s\"\n", ((osEvent*)(thread->StateObject))->GetName() );
         break;
      case SLEEPING:
         pfunc->Printf( "sleeping %luus at %s %d\n",
            thread->USleepTime, thread->Filename,
            thread->Linenumber );
         break;
      }
   }

   Mutex.Give();
}
