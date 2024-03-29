#include "Thread.h"         // Header
#include "StackTrace.h"     // MQTT (Utilities)

#undef malloc
#undef realloc
#undef free

#include <errno.h>          // POSIX
#include <unistd.h>         // POSIX
#include <sys/time.h>       // POSIX
#include <fcntl.h>          // POSIX
#include <stdio.h>          // C Standard
#include <sys/stat.h>       // POSIX
#include <limits.h>         // C Standard
#include <memory.h>         // C Standard (<string.h>)
#include <stdlib.h>         // C Standard

#if defined(USE_NAMED_SEMAPHORES)

#pragma mark - Definitions

#define MAX_NAMED_SEMAPHORES 10

static struct
{
    sem_t* sem;
    char name[NAME_MAX-4];
} named_semaphores[MAX_NAMED_SEMAPHORES];

#pragma mark - Variables

static int named_semaphore_count = 0;

#endif

#pragma mark - Public API

pthread_mutex_t* Thread_create_mutex()
{
	FUNC_ENTRY;
    pthread_mutex_t* mutex = malloc(sizeof(pthread_mutex_t));
    int rc = pthread_mutex_init(mutex, NULL);
	FUNC_EXIT_RC(rc);
	return mutex;
}

int Thread_lock_mutex(pthread_mutex_t* mutex)
{
	// Don't add entry/exit trace points as the stack log uses mutexes - recursion beckons
    int rc = pthread_mutex_lock(mutex);
	return rc;
}

int Thread_unlock_mutex(pthread_mutex_t* mutex)
{
	// Don't add entry/exit trace points as the stack log uses mutexes - recursion beckons
    int rc = pthread_mutex_unlock(mutex);
	return rc;
}

void Thread_destroy_mutex(pthread_mutex_t* mutex)
{
	FUNC_ENTRY;
    int rc = pthread_mutex_destroy(mutex);
    free(mutex);
	FUNC_EXIT_RC(rc);
}

sem_t* Thread_create_sem()
{
	sem_t* sem = NULL;
	int rc = 0;

	FUNC_ENTRY;
    #if defined(USE_NAMED_SEMAPHORES)
    if (named_semaphore_count == 0) { memset(named_semaphores, '\0', sizeof(named_semaphores)); }
    char* name = &(strrchr(tempnam("/", "MQTT"), '/'))[1]; // Skip first slash of name.
    if ((sem = sem_open(name, O_CREAT, S_IRWXU, 0)) == SEM_FAILED)
    {
        rc = -1;
    }
    else
    {
        int i;
    		
    	named_semaphore_count++;
    	for (i = 0; i < MAX_NAMED_SEMAPHORES; ++i)
    	{
    		if (named_semaphores[i].name[0] == '\0')
    		{ 
    			named_semaphores[i].sem = sem;
    			strcpy(named_semaphores[i].name, name);	
    			break;
    		}
    	}
    }
	#else
    sem = malloc(sizeof(sem_t));
    rc = sem_init(sem, 0, 0);
	#endif
	FUNC_EXIT_RC(rc);
	return sem;
}

int Thread_wait_sem(sem_t* sem, int timeout)
{
    // sem_timedwait is the obvious call to use, but seemed not to work on the Viper, so I've used trywait in a loop instead.
	int rc = -1;
	int interval = 10000; // 10000 microseconds: 10 milliseconds
	int count = (1000 * timeout) / interval; // How many intervals in timeout period

	FUNC_ENTRY;
    int i = 0;
	while (++i < count && (rc = sem_trywait(sem)) != 0)
	{
		if (rc == -1 && ((rc = errno) != EAGAIN))
		{
			rc = 0;
			break;
		}
		usleep(interval); // microseconds - .1 of a second
	}
 	FUNC_EXIT_RC(rc);
 	return rc;
}

int Thread_check_sem(sem_t* sem)
{
	int semval = -1;
	sem_getvalue(sem, &semval);
	return semval > 0;
}

int Thread_post_sem(sem_t* sem)
{
	int rc = 0;
	FUNC_ENTRY;
    if (sem_post(sem) == -1) { rc = errno; }
 	FUNC_EXIT_RC(rc);
    return rc;
}

int Thread_destroy_sem(sem_t* sem)
{
	int rc = 0;
	FUNC_ENTRY;
    #if defined(USE_NAMED_SEMAPHORES)
    int i;
    rc = sem_close(sem);
    for (i = 0; i < MAX_NAMED_SEMAPHORES; ++i)
    {
    	if (named_semaphores[i].sem == sem)
    	{ 
    		rc = sem_unlink(named_semaphores[i].name);
    		named_semaphores[i].name[0] = '\0';	
    		break;
    	}
    }
    named_semaphore_count--;
	#else
	rc = sem_destroy(sem);
	free(sem);
	#endif
	FUNC_EXIT_RC(rc);
	return rc;
}

cond_type_struct* Thread_create_cond()
{
	FUNC_ENTRY;
	cond_type_struct* condvar = malloc(sizeof(cond_type_struct));
	int rc = pthread_cond_init(&condvar->cond, NULL);
	rc = pthread_mutex_init(&condvar->mutex, NULL);
	FUNC_EXIT_RC(rc);
	return condvar;
}

int Thread_signal_cond(cond_type_struct* condvar)
{
	pthread_mutex_lock(&condvar->mutex);
	int rc = pthread_cond_signal(&condvar->cond);
	pthread_mutex_unlock(&condvar->mutex);

	return rc;
}

int Thread_wait_cond(cond_type_struct* condvar, int timeout)
{
	FUNC_ENTRY;
	struct timeval cur_time;
	gettimeofday(&cur_time, NULL);

    struct timespec cond_timeout;
	cond_timeout.tv_sec = cur_time.tv_sec + timeout;
	cond_timeout.tv_nsec = cur_time.tv_usec * 1000;

	pthread_mutex_lock(&condvar->mutex);
	int rc = pthread_cond_timedwait(&condvar->cond, &condvar->mutex, &cond_timeout);
	pthread_mutex_unlock(&condvar->mutex);

	FUNC_EXIT_RC(rc);
	return rc;
}

int Thread_destroy_cond(cond_type_struct* condvar)
{
	int rc = pthread_mutex_destroy(&condvar->mutex);
	rc = pthread_cond_destroy(&condvar->cond);
	free(condvar);
	return rc;
}

pthread_t Thread_start(thread_fn fn, void* parameter)
{
    FUNC_ENTRY;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    pthread_t thread = 0;
    if ( pthread_create(&thread, &attr, fn, parameter) != 0 ) { thread = 0; }
    pthread_attr_destroy(&attr);
    FUNC_EXIT;
    return thread;
}

pthread_t Thread_getid()
{
    return pthread_self();
}
