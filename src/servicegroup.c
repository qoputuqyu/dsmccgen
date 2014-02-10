
#include <sys/timeb.h>    /* for timeb to get millitime */
#include <time.h>
#include "dsmcc.h"
#include "servicegroup.h"

static pthread_mutex_t sg_mutex  = PTHREAD_MUTEX_INITIALIZER;


stb_t stb;

static gboolean b_debug = FALSE;

#define STUCK_TOOLONG 50

gboolean
sg_init( servicegroup_t *sgptr, gboolean b_clear )
{
    struct timeval dwell_time_period;
    guint i;
    stb_t *stbptr;
    
    if ( sgptr == NULL )
        return FALSE;
    
    sgptr->thread = 0;
    
    if ( b_clear )
    {
        sgptr->srvrptr = 0;
        sgptr->servicegroup = 0;

        sgptr->stbbase = 0;
        sgptr->stbcnt = 0;

        sgptr->holdoff = 0;
        sgptr->rate = 0;
        sgptr->dwell = 0;

        sgptr->srcidmin = 0;
        sgptr->srcidmax = 0;

        sgptr->flags = 0;
        
        sgptr->stbbegin = NULL;
        sgptr->stbend = NULL;
    }
    else
    {
        g_printf( "Initing service group %i\n", sgptr->servicegroup );
        
        if ( sgptr->stbcnt == 0 )
        {
            g_printf( "no stb\n" );
            return FALSE;
        }

        i = sgptr->stbcnt * sizeof ( stb_t );
        sgptr->stbbegin = malloc( i );
        if ( sgptr->stbbegin == NULL )
        {
            perror( " stb alloc" );
            return FALSE;
        }
    
        sgptr->stbend = &(sgptr->stbbegin[ sgptr->stbcnt - 1 ]);
    
        dwell_time_period.tv_sec = sgptr->dwell;
        dwell_time_period.tv_usec = 0;
    
        stbptr = sgptr->stbbegin;
        for ( i = 1; i <= sgptr->stbcnt; i++, stbptr++ )
            stb_init( stbptr, sgptr->servicegroup, sgptr->srvrptr, 
                      sgptr->stbbase, i, sgptr->flags, dwell_time_period );

        g_printf( "finished init service group %i\n", sgptr->servicegroup );
    }
    
    return TRUE;
} /* sg_init */

void
purge( servicegroup_t *sgptr )
{
    recv_data( sgptr->srvrptr, (gchar *)&stb.dsmcc, sizeof stb.dsmcc );
    g_printf( "purge %s\n", sessionId_to_string( stb.dsmcc.sdb_init_request.sessId ) );
} /* purge */


void
print_log( struct timeval *tvptr, stb_t *stbptr )
{
    gchar timestr[60];

    strftime( timestr, sizeof timestr, "%a %d%b%Y %H:%M:%S",
              localtime( &tvptr->tv_sec ) );
    g_printf( "%s.%03i   sg%-5i ", timestr, tvptr->tv_usec / 1000,
               stbptr->servicegroup );
} /* print_log */


void
check_for_data( servicegroup_t *sgptr )
{
    static gint stuck_cnt = 0;
    
    gint cmpr;
    
    stb_t *stb_b_itr;
    stb_t *stb_e_itr;
    stb_t *stb_m_itr;
    
    server_t *svrptr = sgptr->srvrptr;

    pthread_mutex_lock( &sg_mutex ); 
    
    if ( stuck_cnt > STUCK_TOOLONG && is_data( svrptr ) )
    {   /* remove the blockage - problematic packet */
        purge( sgptr );
        stuck_cnt = 0;
    }
    
    cmpr = 0; /* just to get into the loop */
    while ( cmpr == 0 && is_data( svrptr ) && ( abort_request == 0 ) )
    {   /* here if a message is waiting */

        /* check to see if it is one of our stbs */
        peek_data ( svrptr, (gchar *)&stb.dsmcc , sizeof stb.dsmcc );
        
        if ( sgptr->stbbase != stb.dsmcc.sdb_init_request.sessId[1] )
        {   /* not in the group, so dont even try and find it */
            stuck_cnt++;
            break;
        }
        
        /* here if stb in rcv buff is part of this group */
        stb_b_itr = sgptr->stbbegin;
        stb_e_itr = sgptr->stbend;
        while ( ( stb_b_itr <= stb_e_itr ) && ( abort_request == 0 ) )
        {
            stb_m_itr = stb_b_itr + ( ( stb_e_itr - stb_b_itr ) >> 1 );
            
            /* really only need look at macaddr[4:5] bytes */
            cmpr = stbcmp( stb_m_itr->macaddr,
                           stb.dsmcc.sdb_init_request.sessId );   

            /* test relationship of stb to stb_m_itr */
            if ( cmpr == 0 )
            {   /* it is one of ours, so pull it out of the buffer */
                /* proccess the message and maybe show stuff */
                if ( !stb_dsmcc_in( stb_m_itr ) && stb_m_itr->flags & VERBOSEIN )
                    g_printf( "sg%u stb_dsmcc_in failed\n", sgptr->servicegroup  );

                /* reset stuck counter and get out of the search routine */
                stuck_cnt = 0;
                break;
            }
            else if ( cmpr < 0 )
            {   /* sg stb stb_m_itr is less than queued msg, so move bgn up */
                stb_b_itr = stb_m_itr + 1;
            }
            else
            {   /* sg stb stb_m_itr is gretaer then queued msg, so move end dn */    
                stb_e_itr = stb_m_itr - 1;
            }
        } /* while search */
    } /* while found and is_data */
    
    pthread_mutex_unlock( &sg_mutex ); 
} /* check_for_data */


#define TVTIMENOVA 0xFF000000

/*  time_t time(time_t *t) returns seconds since epoc 00:00:00 1Jan1970
 *                Under BSD 4.3, this call is obsoleted by gettimeofday(2)
 *
 *  int ftime(struct timeb *tp);
 *            struct timeb {
 *                   time_t   time;
 *                   unsigned short millitm;
 *                   short    timezone;
 *                   short    dstflag;
 *            };
 *            time is the number of seconds since the epoch
 *            millitm is the number of milliseconds
 *            This function is obsolete. Don’t use it.
 *            gettimeofday(2) gives microseconds.
 *
 *
 *
 *    usec (dec)   usec (hex)    msec (dec)   msec (hex)
 *          1000 = 000003E8               1 = 00000001
 *          2000 = 000007D0               2 = 00000002
 *        999000 = 000F3E58             999 = 000003E7
 *       
 */




void *
sg_run_task( void *ptr )
{
    guint sourceId;
    guint32 i;

    struct timeval tmval; /* use system time to minimize timing jitter */
//    guint this_time;
    guint sleep_time;

    gboolean b_run;
    gboolean b_send;
    
//    guint sendcnt;
    
    gchar *buff;
    
    servicegroup_t *sgptr;
    server_t *svrptr;
    stb_t *stbptr;
    stb_t *stbitr;
    
    gboolean b_gatedmsg;
    gboolean b_timeout;

    /* some pointers to help with code clarity */
    sgptr = (servicegroup_t *)ptr;
    svrptr = sgptr->srvrptr;

    /* set arting source id */
    sourceId = sgptr->srcidmin;

    if ( !init_channel( svrptr ) )
    {   /* here if server socket setup failed */
        g_printf( "dsmcc_init failed" );
        return;
    }
    
    g_printf( "running task sg%i\n", sgptr->servicegroup );

    stbptr = sgptr->stbbegin;

    /* calculate first time step as now and sleep times */
    /* get seconds and useconds for timing */
    if ( gettimeofday( &tmval, NULL) != 0 )
        perror( "sq_run_task gettimeofday: " );
    
    /* unext_time is an effort to minimize timing jitter */
    sgptr->next_time = tmval;
    
    sleep_time = sgptr->rate >> SLEEPRATE; /* sleep in 1/4 rate steps */
    
//    sendcnt = ( 1 << SLEEPRATE ) - 1;
    b_run = TRUE;
    while ( abort_request == 0 && b_run )
    {
        /* get seconds and useconds for timing */
        if ( gettimeofday( &tmval, NULL) != 0 )
            perror( "sq_run_task gettimeofday: " );
            
        b_timeout = ( tmval.tv_sec > sgptr->next_time.tv_sec ) ||
                      ( tmval.tv_sec == sgptr->next_time.tv_sec &&
                        tmval.tv_usec > sgptr->next_time.tv_usec );
        
        /* clear flags that will be set inside loop */
        b_run = FALSE;
        for ( stbitr = sgptr->stbbegin; stbitr <= sgptr->stbend; stbitr++ )
        {
            /* run the machine */
            stb_FSM( stbitr, &sourceId, sgptr->srcidmin, sgptr->srcidmax );

            /* flag any stbs that are still running */
            b_run |= ( stbitr->state != e_state_done );
            
            b_gatedmsg = stbitr->msgId == DSMCC_MSGID_SDV_INIT_REQUEST ||
                         stbitr->msgId == DSMCC_MSGID_SDV_SELECT_REQUEST;
            
            /* is stbptr sending a gated message */
            b_send = stbitr->state == e_state_tx && stbitr == stbptr &&
                     b_timeout && b_gatedmsg;
    
            /* is stbitr send'g non-gated message */
            b_send |= stbitr->state == e_state_tx && !b_gatedmsg;
    
            if ( b_send )
            {   /* here iff stb is transmiting */
                /* lock access to common resources */
                pthread_mutex_lock( &sg_mutex ); 
                    
                /* sync state info to packet data and send it out */
                stb_dsmcc_out( stbitr );
                
                /* unlock access to common resources */
                pthread_mutex_unlock( &sg_mutex ); 
            }
        } /* for ( ) */
        
               
        if ( b_timeout )
        {   /* here iff unext_time has expried */
            /* refresh next_time */
            sgptr->next_time.tv_usec += sgptr->rate;
            while ( sgptr->next_time.tv_usec >= SECOND_UTIME )
            {
                sgptr->next_time.tv_sec++;
                sgptr->next_time.tv_usec -= SECOND_UTIME;
            }
            
            if ( ++stbptr > sgptr->stbend )
                 stbptr = sgptr->stbbegin;
        }
        
        /* calculate next sleep period */
        i =  ( sgptr->next_time.tv_sec - tmval.tv_sec ) * SECOND_UTIME +
             ( sgptr->next_time.tv_usec - tmval.tv_usec );
             
        if ( i > sleep_time )
            i = sleep_time;
            
        usleep( i );
        
        /* check for sdv responses */
        check_for_data( sgptr );
    } /* end while */

    /* dump the service group stats without interruption */
    pthread_mutex_lock( &sg_mutex );
    
    if ( sgptr->flags & STBDUMP )
    {
        g_printf( "service group %u stb dump\n", sgptr->servicegroup );
        
        for ( stbptr = sgptr->stbbegin; stbptr <= sgptr->stbend; stbptr++ )
            print_stb( stbptr );
        
        g_printf( "\n" );
    }

    g_printf( "service group %i task ", sgptr->servicegroup );
    if ( abort_request == 0 )
        g_printf( "terminated normally\n\n\n" );
    else
        g_printf( " aborted\n\n\n" );

    pthread_mutex_unlock( &sg_mutex );
}

void
sg_free( servicegroup_t *sgptr )
{
    g_printf( "freeing servcie group %i\n", sgptr->servicegroup );
    free( sgptr->stbbegin );
}
