// Matt Wells, Copyright Apr 2001

// . core class for handling interupts-based i/o on non-blocking descriptors
// . when an fd/state/callback is registered for reading we call your callback //   when fd has a read event (same for write and sleeping)

#ifndef _LOOP_H_
#define _LOOP_H_

#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>      // fcntl()
#include <sys/poll.h>   // POLLIN, POLLPRI, ...
#ifndef F_SETSIG
#define F_SETSIG 10     // F_SETSIG
#endif
#include "Mem.h"        // mmalloc, mfree
#define QUERYPRIORITYWEIGHT 16
#define QUICKPOLL_INTERVAL 10

#define sleep(a) { char *xx=NULL;*xx=0; }
//#define sleep(a) logf(LOG_INFO,"sleep: sleep"); 

// we have 2 arrays of slots, m_readSlots and m_writeSlots
class Slot {
 public:
	void   *m_state;
	void  (* m_callback)(int fd, void *state);
	// the next Slot thats registerd on this fd
	Slot   *m_next;
	// save niceness level for doPoll() to segregate
	long    m_niceness;
	// last time we called m_callback for this fd
	//	time_t  m_lastActivity;
	// . when should this fd timeout and we call the callback with
	//   errno set to ETIMEDOUT
	// . set to -1 for never timeout
	// . m_timeout is in seconds
	//	long    m_timeout;     
	// this callback should be called every X milliseconds
	long      m_tick;
	// when we were last called in ms time (only valid for sleep callbacks)
	long long m_lastCall;
	// linked list of available slots
	Slot     *m_nextAvail;
};

#define  sleep(a) { char *xx=NULL;*xx=0; }
#define usleep(a) { char *xx=NULL;*xx=0; }
//#define sleep(a) logf(LOG_INFO,"sleep: sleep"); 

// linux 2.2 kernel has this limitation
#define MAX_NUM_FDS 1024


// . niceness can only be 0, 1 or 2
// . we use 0 for query traffic
// . we use 1 for merge disk threads
// . we use 2 for indexing/spidering
// . 0  will use the high priority udp server, g_udpServer2
// . 1+ will use the low  priority udp server, g_udpServer
// . 0  niceness for disk threads will cancel other threads before launching
// . 1+ niceness threads will be set to lowest priority using setpriority()
// . 1  niceness disk thread, when running, will not allow niceness 2 to launch
//#define MAX_NICENESS 2

// are we using async signal handlers?
extern bool g_isHot;

// extern variable that let's us know if we're in a sig handler
extern bool g_inSigHandler;

// a debugging tool really
extern bool g_interruptsOn ;

// are there pending signals for which we should call g_udpServer.makeCallbacks
extern bool g_someAreQueued;

// . so sig handler can get an approx time of day
// . gettimeofday() is not async signal safe
// . this is now the "local" time, not synced
extern long long g_now;

// . this is now the time synced with host #0
//extern long long g_nowGlobal;

//this is the approximate time generated by the timer.
extern long long g_nowApprox;

// count of how many SIGVTALRM signals we had so far
extern long g_numAlarms;
extern long g_numVTAlarms;
extern long g_numQuickPolls;

extern char g_niceness ;

// we make sure the same callback/handler is not hogging the cpu when it is
// niceness 0 and we do not interrupt it, so this is a critical check
extern class UdpSlot *g_callSlot;

class Loop {

 public:

	// contructor and stuff
	Loop();
	~Loop();

	// free up all our mem
	void reset();

	// set up the signal handlers or block the signals for queueing
	bool init();
	
	// . call this to begin polling/selecting of all registed fds
	// . returns false on error
	bool runLoop();

	// . register this "fd" with "callback"
	// . "callback" will be called when fd is ready for reading
	// . "timeout" is -1 if this never timesout
	bool registerReadCallback  ( int   fd    ,
				     void *state , 
				     void (* callback)(int fd,void *state ) ,
				     long  niceness );//= MAX_NICENESS ) ;

	// . register this "fd" with "callback"
	// . "callback" will be called when fd is ready for reading
	// . "callback" will be called when there is an error on fd
	bool registerWriteCallback ( int   fd    ,
				     void *state , 
				     void (* callback)(int fd, void *state ) ,
				     long   niceness );//= MAX_NICENESS );

	// . register this callback to be called every second
	// . TODO: implement "seconds" parameter
	bool registerSleepCallback ( long milliseconds ,
				     void *state, 
				     void (* callback)(int fd,void *state ) ,
				     long niceness = 1 );

	// unregister call back for reading, writing or sleeping
	void unregisterReadCallback  ( int fd, void *state ,
				       void (* callback)(int fd,void *state),
				       bool silent = false );
	void unregisterWriteCallback ( int fd, void *state ,
				       void (* callback)(int fd,void *state));
	void unregisterSleepCallback ( void *state ,
				       void (* callback)(int fd,void *state));

	// sets up for signal capture by us, g_loop
	bool setNonBlocking ( int fd , long niceness ) ;

	// . keep this public so sighandler() can call it
	// . we also call it from HttpServer::getMsgPieceWrapper() to
	//   notify a socket that it's m_sendBuf got some new data to send
	void callCallbacks_ass (bool forReading, int fd, long long now = 0LL,
				long niceness = -1 );

	// set to true by sigioHandler() so doPoll() will be called
	bool m_needToPoll;

	
	long long   m_lastPollTime;
	bool        m_inQuickPoll;
	bool        m_needsToQuickPoll;
	bool        m_canQuickPoll;
	itimerval   m_quickInterrupt;
	itimerval   m_realInterrupt;
	itimerval   m_noInterrupt;
	// call this when you don't want to be interrupted
	void interruptsOff ( ) ;
	// and this to resume being interrupted
	void interruptsOn ( ) ;

	// the sighupHandler() will set this to 1 when we receive
	// a SIGHUP, 2 if a thread crashed, 3 if we got a SIGPWR
	char m_shutdown;

	void startBlockedCpuTimer();
	void canQuickPoll(long niceness);
	void setitimerInterval(long niceness);

	void disableTimer();
	void enableTimer();

	void quickPoll(long niceness, const char* caller = NULL, long lineno = 0);

	// called when sigqueue overflows and we gotta do a select() or poll()
	void doPoll ( );
 private:


	void unregisterCallback ( Slot **slots , int fd , void *state ,
				  void (* callback)(int fd,void *state) ,
				  bool silent = false );

	bool addSlot ( bool forReading , int fd , void *state , 
		       void (* callback)(int fd , void *state ) ,
		       long niceness , long tick = 0x7fffffff ) ;

	// set how long to pause waiting for singals (in milliseconds)
	void setSigWaitTime ( long ms ) ;

	// now we use a linked list of pre-allocated slots to avoid a malloc
	// failure which can cause the merge to dump with "URGENT MERGE FAILED"
	// message becaise it could not register the sleep wrapper to wait
	Slot *getEmptySlot (         ) ;
	void  returnSlot   ( Slot *s ) ;

	// . these arrays map an fd to a Slot (see above for Slot definition)
	// . that slot may chain to other slots if more than one procedure
	//   is waiting on a file to become available for reading/writing
	// . these fd's are real, not virtual
	// . m_read/writeFds[i] is NULL if no one is waiting on fd #i
	// . fd of MAX_NUM_FDS   is used for sleep callbacks
	// . fd of MAX_NUM_FDS+1 is used for thread exit callbacks
	Slot *m_readSlots  [MAX_NUM_FDS+2];
	Slot *m_writeSlots [MAX_NUM_FDS+2];

	// the minimal tick time in milliseconds (ms)
	long m_minTick;

	// now we pre-allocate our slots to prevent nasty coredumps from merge
	// because it could not register a sleep callback with us
	Slot *m_slots;
	Slot *m_head;
	Slot *m_tail;
};

extern class Loop g_loop;

//#define QUICKPOLL(a) if(g_loop.m_canQuickPoll && g_loop.m_needsToQuickPoll) g_loop.quickPoll(a, __PRETTY_FUNCTION__, __LINE__)
#define QUICKPOLL(a) if(g_niceness && g_loop.m_needsToQuickPoll) g_loop.quickPoll(a, __PRETTY_FUNCTION__, __LINE__)

#endif
