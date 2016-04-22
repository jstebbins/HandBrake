/* reader.c

   Copyright (c) 2003-2016 HandBrake Team
   This file is part of the HandBrake source code
   Homepage: <http://handbrake.fr/>.
   It may be used under the terms of the GNU General Public License v2.
   For full terms see the file COPYING file or visit http://www.gnu.org/licenses/gpl-2.0.html
 */
#include "hb.h"

static int  reader_init( hb_work_object_t * w, hb_job_t * job );
static void reader_close( hb_work_object_t * w );
static int  reader_work( hb_work_object_t * w, hb_buffer_t ** buf_in,
                         hb_buffer_t ** buf_out);

hb_work_object_t hb_reader =
{
    .id     = WORK_READER,
    .name   = "Reader",
    .init   = reader_init,
    .work   = reader_work,
    .close  = reader_close,
    .info   = NULL,
    .bsinfo = NULL,
    .flush  = NULL
};

typedef struct
{
    int              id;
    hb_buffer_list_t list;
} buffer_splice_list_t;

struct hb_work_private_s
{
    hb_handle_t  * h;
    hb_job_t     * job;
    hb_title_t   * title;
    volatile int * die;

    hb_bd_t      * bd;
    hb_dvd_t     * dvd;
    hb_stream_t  * stream;

    hb_psdemux_t   demux;
    int            scr_changes;
    int64_t        scr_offset;
    int64_t        last_pts;

    int            start_found;     // found pts_to_start point
    int64_t        pts_to_start;
    int            chapter_end;
    hb_fifo_t   ** fifos;

    buffer_splice_list_t * splice_list;
    int                    splice_list_size;
};

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static hb_fifo_t ** GetFifoForId( hb_work_private_t * r, int id );
static hb_buffer_list_t * get_splice_list(hb_work_private_t * r, int id);

/***********************************************************************
 * reader_init
 ***********************************************************************
 *
 **********************************************************************/
static int hb_reader_open( hb_work_private_t * r )
{
    if ( r->title->type == HB_BD_TYPE )
    {
        if ( !( r->bd = hb_bd_init( r->h, r->title->path ) ) )
            return 1;
        if(!hb_bd_start(r->bd, r->title))
        {
            hb_bd_close(&r->bd);
            return 1;
        }
        if (r->job->angle > 1)
        {
            hb_bd_set_angle(r->bd, r->job->angle - 1);
        }
        if (r->job->start_at_preview)
        {
            // XXX code from DecodePreviews - should go into its own routine
            hb_bd_seek(r->bd, (float)r->job->start_at_preview /
                       (r->job->seek_points ? (r->job->seek_points + 1.0)
                                            : 11.0));
        }
        else if (r->job->pts_to_start)
        {
            // Note, bd seeks always put us to an i-frame.  no need
            // to start decoding early using r->pts_to_start
            hb_bd_seek_pts(r->bd, r->job->pts_to_start);
            r->job->pts_to_start = 0;
            r->start_found = 1;
        }
        else
        {
            hb_bd_seek_chapter(r->bd, r->job->chapter_start);
        }
    }
    else if (r->title->type == HB_DVD_TYPE)
    {
        if ( !( r->dvd = hb_dvd_init( r->h, r->title->path ) ) )
            return 1;
        if(!hb_dvd_start( r->dvd, r->title, r->job->chapter_start))
        {
            hb_dvd_close(&r->dvd);
            return 1;
        }
        if (r->job->angle)
        {
            hb_dvd_set_angle(r->dvd, r->job->angle);
        }

        if (r->job->start_at_preview)
        {
            hb_dvd_seek(r->dvd, (float)r->job->start_at_preview /
                        (r->job->seek_points ? (r->job->seek_points + 1.0)
                                             : 11.0));
        }
        // libdvdnav doesn't have a seek to timestamp function.
        // So we will have to decode frames until we find the correct time
        // in sync.c
        r->start_found = 1;
    }
    else if (r->title->type == HB_STREAM_TYPE ||
             r->title->type == HB_FF_STREAM_TYPE)
    {
        if (!(r->stream = hb_stream_open(r->h, r->title->path, r->title, 0)))
            return 1;
        if (r->job->start_at_preview)
        {
            hb_stream_seek(r->stream, (float)(r->job->start_at_preview - 1) /
                           (r->job->seek_points ? (r->job->seek_points + 1.0)
                                                : 11.0));
        }
        else if (r->job->pts_to_start)
        {
            if (hb_stream_seek_ts( r->stream, r->job->pts_to_start ) >= 0)
            {
                // Seek takes us to the nearest I-frame before the timestamp
                // that we want.  So we will retrieve the start time of the
                // first packet we get, subtract that from pts_to_start, and
                // inspect the reset of the frames in sync.
            }
            else
            {
                // hb_stream_seek_ts does nothing for TS streams and will
                // return an error.
                //
                // So we will decode frames until we find the correct time
                // in sync.c
                r->start_found = 1;
            }
        }
        else
        {
            //
            // Standard stream, seek to the starting chapter, if set,
            // and track the end chapter so that we end at the right time.
            hb_chapter_t *chap;
            int start = r->job->chapter_start;
            chap = hb_list_item(r->job->list_chapter, r->job->chapter_end - 1);

            r->chapter_end = chap->index;
            if (start > 1)
            {
                chap = hb_list_item(r->job->list_chapter, start - 1);
                start = chap->index;
            }

            /*
             * Seek to the start chapter.
             */
            hb_stream_seek_chapter(r->stream, start);
        }
    }
    else
    {
        // Unknown type, should never happen
        return 1;
    }

    return 0;
}

static int reader_init( hb_work_object_t * w, hb_job_t * job )
{
    hb_work_private_t * r;

    r = calloc( sizeof( hb_work_private_t ), 1 );
    w->private_data = r;

    r->h     = job->h;
    r->job   = job;
    r->title = job->title;
    r->die   = job->die;

    r->demux.last_scr = AV_NOPTS_VALUE;
    r->last_pts       = AV_NOPTS_VALUE;

    r->chapter_end = job->chapter_end;
    if ( !job->pts_to_start )
        r->start_found = 1;
    else
    {
        // The frame at the actual start time may not be an i-frame
        // so can't be decoded without starting a little early.
        // sync.c will drop early frames.
        // Starting a little over 10 seconds early
        r->pts_to_start = MAX(0, job->pts_to_start - 1000000);
    }

    // Count number of splice lists needed for merging buffers
    // that have been split
    int count = 1; // 1 for video
    count += hb_list_count( job->list_subtitle );
    count += hb_list_count( job->list_audio );
    r->splice_list_size = count;
    r->splice_list = calloc(count, sizeof(buffer_splice_list_t));

    // Initialize stream id's of splice lists
    int ii, jj = 0;
    r->splice_list[jj++].id = r->title->video_id;
    for (ii = 0; ii < hb_list_count(job->list_subtitle); ii++)
    {
        hb_subtitle_t * subtitle = hb_list_item(job->list_subtitle, ii);
        r->splice_list[jj++].id = subtitle->id;
    }
    for (ii = 0; ii < hb_list_count(job->list_audio); ii++)
    {
        hb_audio_t * audio = hb_list_item(job->list_audio, ii);
        r->splice_list[jj++].id = audio->id;
    }

    // count also happens to be the upper bound for the number of
    // fifos that will be needed (+1 for null terminator)
    r->fifos = calloc(count + 1, sizeof(hb_fifo_t*));

    // The stream needs to be open before starting the reader thead
    // to prevent a race with decoders that may share information
    // with the reader. Specifically avcodec needs this.
    if ( hb_reader_open( r ) )
    {
        free( r );
        return 1;
    }
    return 0;
}


static void reader_close( hb_work_object_t * w )
{
    hb_work_private_t * r = w->private_data;

    if ( r == NULL )
    {
        return;
    }
    if (r->bd)
    {
        hb_bd_stop( r->bd );
        hb_bd_close( &r->bd );
    }
    else if (r->dvd)
    {
        hb_dvd_stop( r->dvd );
        hb_dvd_close( &r->dvd );
    }
    else if (r->stream)
    {
        hb_stream_close(&r->stream);
    }

    int ii;
    for (ii = 0; ii < r->splice_list_size; ii++)
    {
        hb_buffer_list_close(&r->splice_list[ii].list);
    }

    free(r->fifos);
    free(r->splice_list);
    free(r);
}

static hb_buffer_t * splice_discontinuity( hb_work_private_t *r, hb_buffer_t *buf )
{
    // Handle buffers that were split across a PCR discontinuity.
    // Rejoin them into a single buffer.
    hb_buffer_list_t * list = get_splice_list(r, buf->s.id);
    if (list != NULL)
    {
        hb_buffer_list_append(list, buf);
        if (buf->s.split)
        {
            return NULL;
        }

        int count = hb_buffer_list_count(list);
        if (count > 1)
        {
            int size = hb_buffer_list_size(list);
            hb_buffer_t * b = hb_buffer_init(size);
            buf = hb_buffer_list_head(list);
            b->s = buf->s;

            int pos = 0;
            while ((buf = hb_buffer_list_rem_head(list)) != NULL)
            {
                memcpy(b->data + pos, buf->data, buf->size);
                pos += buf->size;
                hb_buffer_close(&buf);
            }
            buf = b;
        }
        else
        {
            buf = hb_buffer_list_clear(list);
        }
    }
    return buf;
}

static void push_buf( hb_work_private_t *r, hb_fifo_t *fifo, hb_buffer_t *buf )
{
    while ( !*r->die && !r->job->done )
    {
        if ( hb_fifo_full_wait( fifo ) )
        {
            hb_fifo_push( fifo, buf );
            buf = NULL;
            break;
        }
    }
    if ( buf )
    {
        hb_buffer_close( &buf );
    }
}

static void reader_send_eof( hb_work_private_t * r )
{
    int ii;

    // send eof buffers downstream to decoders to signal we're done.
    push_buf(r, r->job->fifo_mpeg2, hb_buffer_eof_init());

    hb_audio_t *audio;
    for (ii = 0; (audio = hb_list_item(r->job->list_audio, ii)); ++ii)
    {
        if (audio->priv.fifo_in)
            push_buf(r, audio->priv.fifo_in, hb_buffer_eof_init());
    }

    hb_subtitle_t *subtitle;
    for (ii = 0; (subtitle = hb_list_item(r->job->list_subtitle, ii)); ++ii)
    {
        if (subtitle->fifo_in && subtitle->source != SRTSUB)
            push_buf(r, subtitle->fifo_in, hb_buffer_eof_init());
    }
    hb_log("reader: done. %d scr changes", r->demux.scr_changes);
}

static int reader_work( hb_work_object_t * w, hb_buffer_t ** buf_in,
                        hb_buffer_t ** buf_out)
{
    hb_work_private_t  * r = w->private_data;
    hb_fifo_t         ** fifos;
    hb_buffer_t        * buf;
    hb_buffer_list_t     list;
    int                  ii, chapter = -1;

    hb_buffer_list_clear(&list);

    if (r->bd)
        chapter = hb_bd_chapter( r->bd );
    else if (r->dvd)
        chapter = hb_dvd_chapter( r->dvd );
    else if (r->stream)
        chapter = hb_stream_chapter( r->stream );

    if( chapter < 0 )
    {
        hb_log( "reader: end of the title reached" );
        reader_send_eof(r);
        return HB_WORK_DONE;
    }
    if( chapter > r->chapter_end )
    {
        hb_log("reader: end of chapter %d (media %d) reached at media chapter %d",
                r->job->chapter_end, r->chapter_end, chapter);
        reader_send_eof(r);
        return HB_WORK_DONE;
    }

    if (r->bd)
    {
        if( (buf = hb_bd_read( r->bd )) == NULL )
        {
            reader_send_eof(r);
            return HB_WORK_DONE;
        }
    }
    else if (r->dvd)
    {
        if( (buf = hb_dvd_read( r->dvd )) == NULL )
        {
            reader_send_eof(r);
            return HB_WORK_DONE;
        }
    }
    else if (r->stream)
    {
        if ( (buf = hb_stream_read( r->stream )) == NULL )
        {
            reader_send_eof(r);
            return HB_WORK_DONE;
        }
    }

    (hb_demux[r->title->demuxer])(buf, &list, &r->demux);

    while ((buf = hb_buffer_list_rem_head(&list)) != NULL)
    {
        if (buf->s.start   != AV_NOPTS_VALUE &&
            r->scr_changes != r->demux.scr_changes)
        {
            // First valid timestamp after an SCR change.  Update
            // the per-stream scr sequence number
            r->scr_changes = r->demux.scr_changes;

            // libav tries to be too smart with timestamps and
            // enforces unnecessary conditions.  One such condition
            // is that subtitle timestamps must be monotonically
            // increasing.  To encure this is the case, we calculate
            // an offset upon each SCR change that will guarantee this.
            // This is just a very rough SCR offset.  A fine grained
            // offset that maintains proper sync is calculated in sync.c
            if (r->last_pts != AV_NOPTS_VALUE)
            {
                r->scr_offset  = r->last_pts + 90000 - buf->s.start;
            }
            else
            {
                r->scr_offset  = -buf->s.start;
            }
        }
        // Set the scr sequence that this buffer's timestamps are
        // referenced to.
        buf->s.scr_sequence = r->scr_changes;
        if (buf->s.start != AV_NOPTS_VALUE)
        {
            buf->s.start += r->scr_offset;
        }
        if (buf->s.renderOffset != AV_NOPTS_VALUE)
        {
            buf->s.renderOffset += r->scr_offset;
        }
        if (buf->s.start > r->last_pts)
        {
            r->last_pts = buf->s.start;
        }

        fifos = GetFifoForId( r, buf->s.id );
        if (fifos && r->stream && !r->start_found)
        {
            // libav is allowing SSA subtitles to leak through that are
            // prior to the seek point.  So only make the adjustment to
            // pts_to_start after we see the next video buffer.
            if (buf->s.id != r->job->title->video_id)
            {
                hb_buffer_close(&buf);
                continue;
            }
            // We will inspect the timestamps of each frame in sync
            // to skip from this seek point to the timestamp we
            // want to start at.
            if (buf->s.start != AV_NOPTS_VALUE &&
                buf->s.start < r->job->pts_to_start)
            {
                r->job->pts_to_start -= buf->s.start;
            }
            else if ( buf->s.start >= r->job->pts_to_start )
            {
                r->job->pts_to_start = 0;
            }
            r->start_found = 1;
        }

        buf = splice_discontinuity(r, buf);
        if (fifos && buf != NULL)
        {
            /* if there are mutiple output fifos, send a copy of the
             * buffer down all but the first (we have to not ship the
             * original buffer or we'll race with the thread that's
             * consuming the buffer & inject garbage into the data stream). */
            for (ii = 1; fifos[ii] != NULL; ii++)
            {
                hb_buffer_t *buf_copy = hb_buffer_init(buf->size);
                buf_copy->s = buf->s;
                memcpy(buf_copy->data, buf->data, buf->size);
                push_buf(r, fifos[ii], buf_copy);
            }
            push_buf(r, fifos[0], buf);
            buf = NULL;
        }
        else
        {
            hb_buffer_close(&buf);
        }
    }

    hb_buffer_list_close(&list);
    return HB_WORK_OK;
}

/***********************************************************************
 * GetFifoForId
 ***********************************************************************
 *
 **********************************************************************/
static hb_fifo_t ** GetFifoForId( hb_work_private_t * r, int id )
{
    hb_job_t      * job = r->job;
    hb_title_t    * title = job->title;
    hb_audio_t    * audio;
    hb_subtitle_t * subtitle;
    int             i, n;

    if( id == title->video_id )
    {
        if (job->indepth_scan && !job->frame_to_stop)
        {
            /*
             * Ditch the video here during the indepth scan until
             * we can improve the MPEG2 decode performance.
             *
             * But if we specify a stop frame, we must decode the
             * frames in order to count them.
             */
            return NULL;
        }
        else
        {
            r->fifos[0] = job->fifo_mpeg2;
            r->fifos[1] = NULL;
            return r->fifos;
        }
    }

    for( i = n = 0; i < hb_list_count( job->list_subtitle ); i++ )
    {
        subtitle =  hb_list_item( job->list_subtitle, i );
        if (id == subtitle->id)
        {
            /* pass the subtitles to be processed */
            r->fifos[n++] = subtitle->fifo_in;
        }
    }
    if ( n != 0 )
    {
        r->fifos[n] = NULL;
        return r->fifos;
    }

    if( !job->indepth_scan )
    {
        for( i = n = 0; i < hb_list_count( job->list_audio ); i++ )
        {
            audio = hb_list_item( job->list_audio, i );
            if( id == audio->id )
            {
                r->fifos[n++] = audio->priv.fifo_in;
            }
        }

        if( n != 0 )
        {
            r->fifos[n] = NULL;
            return r->fifos;
        }
    }

    return NULL;
}

static hb_buffer_list_t * get_splice_list(hb_work_private_t * r, int id)
{
    int ii;

    for (ii = 0; ii < r->splice_list_size; ii++)
    {
        if (r->splice_list[ii].id == id)
        {
            return &r->splice_list[ii].list;
        }
    }
    return NULL;
}
