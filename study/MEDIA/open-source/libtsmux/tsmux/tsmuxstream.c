#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "tsmuxcommon.h"
#include "tsmuxstream.h"

static guint8 tsmux_stream_pes_header_length (TsMuxStream * stream);
static void tsmux_stream_write_pes_header (TsMuxStream * stream, guint8 * data);
static void tsmux_stream_find_pts_dts_within (TsMuxStream * stream, guint bound,
    gint64 * pts, gint64 * dts);

struct TsMuxStreamBuffer
{
  guint8 *data;
  guint32 size;

  /* PTS & DTS associated with the contents of this buffer */
  gint64 pts;
  gint64 dts;

  void *user_data;
};

/**
 * tsmux_stream_new:
 * @pid: a PID
 * @stream_type: the stream type
 *
 * Create a new stream with PID of @pid and @stream_type.
 *
 * Returns: a new #TsMuxStream.
 */
TsMuxStream *
tsmux_stream_new (guint16 pid, TsMuxStreamType stream_type)
{
  TsMuxStream *stream = g_new0 (TsMuxStream, 1);

  stream->state = TSMUX_STREAM_STATE_HEADER;
  stream->pi.pid = pid;
  stream->stream_type = stream_type;

  stream->pes_payload_size = 0;
  stream->cur_pes_payload_size = 0;
  stream->pes_bytes_written = 0;

  switch (stream_type) {
    case TSMUX_ST_VIDEO_MPEG1:
    case TSMUX_ST_VIDEO_MPEG2:
    case TSMUX_ST_VIDEO_MPEG4:
    case TSMUX_ST_VIDEO_H264:
      /* FIXME: Assign sequential IDs? */
      stream->id = 0xE0;
      stream->pi.flags |= TSMUX_PACKET_FLAG_PES_FULL_HEADER;
      stream->is_video_stream = TRUE;
      break;
    case TSMUX_ST_AUDIO_AAC:
    case TSMUX_ST_AUDIO_MPEG1:
    case TSMUX_ST_AUDIO_MPEG2:
      /* FIXME: Assign sequential IDs? */
      stream->id = 0xC0;
      stream->pi.flags |= TSMUX_PACKET_FLAG_PES_FULL_HEADER;
      break;
    case TSMUX_ST_VIDEO_DIRAC:
      stream->id = 0xFD;
      /* FIXME: assign sequential extended IDs? */
      stream->id_extended = 0x60;

      stream->pi.flags |= 
           TSMUX_PACKET_FLAG_PES_FULL_HEADER | 
           TSMUX_PACKET_FLAG_PES_EXT_STREAMID;
      stream->is_video_stream = TRUE;
      break;
    default:
      g_critical ("Stream type 0x%0x not yet implemented", stream_type);
      break;
  }

  stream->last_pts = -1;
  stream->last_dts = -1;

  stream->pcr_ref = 0;
  stream->last_pcr = -1;

  return stream;
}

/**
 * tsmux_stream_get_pid:
 * @stream: a #TsMuxStream
 *
 * Get the PID of @stream.
 *
 * Returns: The PID of @stream. 0xffff on error.
 */
guint16
tsmux_stream_get_pid (TsMuxStream *stream)
{
  g_return_val_if_fail (stream != NULL, G_MAXUINT16);

  return stream->pi.pid;
}

/**
 * tsmux_stream_free:
 * @stream: a #TsMuxStream
 *
 * Free the resources of @stream.
 */
void
tsmux_stream_free (TsMuxStream * stream)
{
  g_return_if_fail (stream != NULL);

  g_free (stream);
}

/**
 * tsmux_stream_set_buffer_release_func:
 * @stream: a #TsMuxStream
 * @func: the new #TsMuxStreamBufferReleaseFunc
 *
 * Set the function that will be called when a a piece of data fed to @stream
 * with tsmux_stream_add_data() can be freed. @func will be called with user
 * data as provided with the call to tsmux_stream_add_data().
 */
void
tsmux_stream_set_buffer_release_func (TsMuxStream * stream,
    TsMuxStreamBufferReleaseFunc func)
{
  g_return_if_fail (stream != NULL);

  stream->buffer_release = func;
}

/* Advance the current packet stream position by len bytes.
 * Mustn't consume more than available in the current packet */
static void
tsmux_stream_consume (TsMuxStream * stream, guint len)
{
  g_assert (stream->cur_buffer != NULL);
  g_assert (len <= stream->cur_buffer->size - stream->cur_buffer_consumed);

  stream->cur_buffer_consumed += len;
  stream->bytes_avail -= len;

  if (stream->cur_buffer_consumed == 0)
    return;

  if (stream->cur_buffer->pts != -1) {
    stream->last_pts = stream->cur_buffer->pts;
    stream->last_dts = stream->cur_buffer->dts;
  } else if (stream->cur_buffer->dts != -1)
    stream->last_dts = stream->cur_buffer->dts;

  if (stream->cur_buffer_consumed == stream->cur_buffer->size) {
    /* Current packet is completed, move along */
    stream->buffers = g_list_delete_link (stream->buffers, stream->buffers);

    if (stream->buffer_release) {
      stream->buffer_release (stream->cur_buffer->data,
          stream->cur_buffer->user_data);
    }

    g_free (stream->cur_buffer);
    stream->cur_buffer = NULL;
    /* FIXME: As a hack, for unbounded streams, start a new PES packet for each
     * incoming packet we receive. This assumes that incoming data is 
     * packetised sensibly - ie, every video frame */
    if (stream->cur_pes_payload_size == 0)
      stream->state = TSMUX_STREAM_STATE_HEADER; 
  }
}

/**
 * tsmux_stream_at_pes_start:
 * @stream: a #TsMuxStream
 *
 * Check if @stream is at the start of a PES packet.
 *
 * Returns: TRUE if @stream is at a PES header packet.
 */
gboolean
tsmux_stream_at_pes_start (TsMuxStream * stream)
{
  g_return_val_if_fail (stream != NULL, FALSE);

  return stream->state == TSMUX_STREAM_STATE_HEADER;
}

/**
 * tsmux_stream_bytes_avail:
 * @stream: a #TsMuxStream
 *
 * Calculate how much bytes are available.
 *
 * Returns: The number of bytes available.
 */
gint
tsmux_stream_bytes_avail (TsMuxStream * stream)
{
  gint bytes_avail;

  g_return_val_if_fail (stream != NULL, 0);

  if (stream->cur_pes_payload_size != 0)
    bytes_avail = stream->cur_pes_payload_size - stream->pes_bytes_written;
  else
    bytes_avail = tsmux_stream_bytes_in_buffer (stream);

  bytes_avail = MIN (bytes_avail, tsmux_stream_bytes_in_buffer (stream));

  /* Calculate the number of bytes available in the current PES */
  if (stream->state == TSMUX_STREAM_STATE_HEADER)
    bytes_avail += tsmux_stream_pes_header_length (stream);

  return bytes_avail;
}

/**
 * tsmux_stream_bytes_in_buffer:
 * @stream: a #TsMuxStream
 *
 * Calculate how much bytes are in the buffer.
 *
 * Returns: The number of bytes in the buffer.
 */
gint
tsmux_stream_bytes_in_buffer (TsMuxStream * stream)
{
  g_return_val_if_fail (stream != NULL, 0);

  return stream->bytes_avail;
}

/**
 * tsmux_stream_get_data:
 * @stream: a #TsMuxStream
 * @buf: a buffer to hold the result
 * @len: the length of @buf
 *
 * Copy up to @len available data in @stream into the buffer @buf.
 *
 * Returns: TRUE if @len bytes could be retrieved.
 */
gboolean
tsmux_stream_get_data (TsMuxStream * stream, guint8 * buf, guint len)
{
  g_return_val_if_fail (stream != NULL, FALSE);
  g_return_val_if_fail (buf != NULL, FALSE);

  if (stream->state == TSMUX_STREAM_STATE_HEADER) {
    guint8 pes_hdr_length;

    if (stream->pes_payload_size != 0) {
      /* Use prescribed fixed PES payload size */
      stream->cur_pes_payload_size = stream->pes_payload_size;
      tsmux_stream_find_pts_dts_within (stream, stream->cur_pes_payload_size,
          &stream->pts, &stream->dts);
    } else if (stream->is_video_stream) {
      /* Unbounded for video streams */
      stream->cur_pes_payload_size = 0;
      tsmux_stream_find_pts_dts_within (stream,
          tsmux_stream_bytes_in_buffer (stream), &stream->pts, &stream->dts);
    } else {
      /* Output a PES packet of all currently available bytes otherwise */
      stream->cur_pes_payload_size = tsmux_stream_bytes_in_buffer (stream);
      tsmux_stream_find_pts_dts_within (stream, stream->cur_pes_payload_size,
          &stream->pts, &stream->dts);
    }

    stream->pi.flags &= ~(TSMUX_PACKET_FLAG_PES_WRITE_PTS_DTS |
        TSMUX_PACKET_FLAG_PES_WRITE_PTS);

    if (stream->pts != -1 && stream->dts != -1)
      stream->pi.flags |= TSMUX_PACKET_FLAG_PES_WRITE_PTS_DTS;
    else {
      if (stream->pts != -1)
        stream->pi.flags |= TSMUX_PACKET_FLAG_PES_WRITE_PTS;
    }

    pes_hdr_length = tsmux_stream_pes_header_length (stream);

    /* Submitted buffer must be at least as large as the PES header */
    if (len < pes_hdr_length)
      return FALSE;

    TS_DEBUG ("Writing PES header of length %u and payload %d",
        pes_hdr_length, stream->cur_pes_payload_size);
    tsmux_stream_write_pes_header (stream, buf);

    len -= pes_hdr_length;
    buf += pes_hdr_length;

    stream->state = TSMUX_STREAM_STATE_PACKET;
  }

  if (len > (guint) tsmux_stream_bytes_avail (stream))
    return FALSE;

  stream->pes_bytes_written += len;

  if (stream->cur_pes_payload_size != 0 &&
      stream->pes_bytes_written == stream->cur_pes_payload_size) {
    TS_DEBUG ("Finished PES packet");
    stream->state = TSMUX_STREAM_STATE_HEADER;
    stream->pes_bytes_written = 0;
  }

  while (len > 0) {
    guint32 avail;
    guint8 *cur;

    if (stream->cur_buffer == NULL) {
      /* Start next packet */
      if (stream->buffers == NULL)
        return FALSE;
      stream->cur_buffer = (TsMuxStreamBuffer *) (stream->buffers->data);
      stream->cur_buffer_consumed = 0;
    }

    /* Take as much as we can from the current buffer */
    avail = stream->cur_buffer->size - stream->cur_buffer_consumed;
    cur = stream->cur_buffer->data + stream->cur_buffer_consumed;
    if (avail < len) {
      memcpy (buf, cur, avail);
      tsmux_stream_consume (stream, avail);

      buf += avail;
      len -= avail;
    } else {
      memcpy (buf, cur, len);
      tsmux_stream_consume (stream, len);

      len = 0;
    }
  }

  return TRUE;
}

static guint8
tsmux_stream_pes_header_length (TsMuxStream * stream)
{
  guint8 packet_len;

  /* Calculate the length of the header for this stream */

  /* start_code prefix + stream_id + pes_packet_length = 6 bytes */
  packet_len = 6;

  if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_FULL_HEADER) {
    /* For a PES 'full header' we have at least 3 more bytes, 
     * and then more based on flags */
    packet_len += 3;
    if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_WRITE_PTS_DTS) {
      packet_len += 10;
    } else if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_WRITE_PTS) {
      packet_len += 5;
    }
    if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_EXT_STREAMID) {
      /* Need basic extension flags (1 byte), plus 2 more bytes for the 
       * length + extended stream id */
      packet_len += 3;
    }
  }

  return packet_len;
}

/* Find a PTS/DTS to write into the pes header within the next bound bytes
 * of the data */
static void
tsmux_stream_find_pts_dts_within (TsMuxStream * stream, guint bound,
    gint64 * pts, gint64 * dts)
{
  GList *cur;

  *pts = -1;
  *dts = -1;

  for (cur = g_list_first (stream->buffers); cur != NULL;
      cur = g_list_next (cur)) {
    TsMuxStreamBuffer *curbuf = cur->data;

    /* FIXME: This isn't quite correct - if the 'bound' is within this
     * buffer, we don't know if the timestamp is before or after the split
     * so we shouldn't return it */
    if (bound <= curbuf->size) {
      *pts = curbuf->pts;
      *dts = curbuf->dts;
      return;
    }

    /* Have we found a buffer with pts/dts set? */
    if (curbuf->pts != -1 || curbuf->dts != -1) {
      *pts = curbuf->pts;
      *dts = curbuf->dts;
      return;
    }

    bound -= curbuf->size;
  }
}

static void
tsmux_stream_write_pes_header (TsMuxStream * stream, guint8 * data)
{
  guint16 length_to_write;
  guint8 hdr_len = tsmux_stream_pes_header_length (stream);

  /* start_code prefix + stream_id + pes_packet_length = 6 bytes */
  data[0] = 0x00;
  data[1] = 0x00;
  data[2] = 0x01;
  data[3] = stream->id;
  data += 4;

  /* Write 2 byte PES packet length here. 0 (unbounded) is only
   * valid for video packets */
  if (stream->cur_pes_payload_size != 0) {
    length_to_write = hdr_len + stream->cur_pes_payload_size - 6;
  } else {
    length_to_write = 0;
  }

  tsmux_put16 (&data, length_to_write);

  if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_FULL_HEADER) {
    guint8 flags = 0;

    /* Not scrambled, original, not-copyrighted, data_alignment not specified */
    *data++ = 0x81;

    /* Flags */
    if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_WRITE_PTS_DTS)
      flags |= 0xC0;
    else if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_WRITE_PTS)
      flags |= 0x80;
    if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_EXT_STREAMID)
      flags |= 0x01; /* Enable PES_extension_flag */
    *data++ = flags;

    /* Header length is the total pes length, 
     * minus the 9 bytes of start codes, flags + hdr_len */
    g_return_if_fail (hdr_len >= 9);
    *data++ = (hdr_len - 9);

    if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_WRITE_PTS_DTS) {
      tsmux_put_ts (&data, 0x3, stream->pts);
      tsmux_put_ts (&data, 0x1, stream->dts);
    } else if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_WRITE_PTS) {
      tsmux_put_ts (&data, 0x2, stream->pts);
    }
    if (stream->pi.flags & TSMUX_PACKET_FLAG_PES_EXT_STREAMID) {
      guint8 ext_len;

      flags = 0x0f; /* (reserved bits) | PES_extension_flag_2 */
      *data++ = flags;
      
      ext_len = 1; /* Only writing 1 byte into the extended fields */
      *data++ = 0x80 | ext_len;
      /* Write the extended streamID */
      *data++ = 0x80 | stream->id_extended;
    }
  }
}

/**
 * tsmux_stream_add_data:
 * @stream: a #TsMuxStream
 * @data: data to add
 * @len: length of @data
 * @user_data: user data to pass to release func
 * @pts: PTS of access unit in @data
 * @dts: DTS of access unit in @data
 *
 * Submit @len bytes of @data into @stream. @pts and @dts can be set to the
 * timestamp (against a 90Hz clock) of the first access unit in @data. A
 * timestamp of -1 for @pts or @dts means unknown.
 *
 * @user_data will be passed to the release function as set with
 * tsmux_stream_set_buffer_release_func() when @data can be freed.
 */
void
tsmux_stream_add_data (TsMuxStream * stream, guint8 * data, guint len,
    void *user_data, gint64 pts, gint64 dts)
{
  TsMuxStreamBuffer *packet;

  g_return_if_fail (stream != NULL);
  
  packet = g_new (TsMuxStreamBuffer, 1);
  packet->data = data;
  packet->size = len;
  packet->user_data = user_data;

  packet->pts = pts;
  packet->dts = dts;

  if (stream->bytes_avail == 0)
    stream->last_pts = pts;

  stream->bytes_avail += len;
  stream->buffers = g_list_append (stream->buffers, packet);
}

/**
 * tsmux_stream_get_es_descrs:
 * @stream: a #TsMuxStream
 * @buf: a buffer to hold the ES descriptor
 * @len: the length used in @buf
 *
 * Write an Elementary Stream Descriptor for @stream into @buf. the number of
 * bytes consumed in @buf will be updated in @len.
 *
 * @buf and @len must be at least #TSMUX_MIN_ES_DESC_LEN.
 */
void
tsmux_stream_get_es_descrs (TsMuxStream * stream, guint8 * buf, guint16 * len)
{
  guint8 *pos;

  g_return_if_fail (stream != NULL);

  if (buf == NULL) {
    if (len != NULL)
      *len = 0;
    return;
  }

  /* Based on the stream type, write out any descriptors to go in the 
   * PMT ES_info field */
  pos = buf;

  switch (stream->stream_type) {
    case TSMUX_ST_VIDEO_DIRAC:
      /* tag (registration_descriptor), length, format_identifier */
      *pos++ = 0x05;
      *pos++ = 4;
      *pos++ = 0x64; /* 'd' */
      *pos++ = 0x72; /* 'r' */
      *pos++ = 0x61; /* 'a' */
      *pos++ = 0x63; /* 'c' */
      break;
    default:
      break;
  }

  if (len)
    *len = (pos - buf);
}

/**
 * tsmux_stream_pcr_ref:
 * @stream: a #TsMuxStream
 *
 * Mark the stream as being used as the PCR for some program.
 */
void
tsmux_stream_pcr_ref (TsMuxStream * stream)
{
  g_return_if_fail (stream != NULL);

  stream->pcr_ref++;
}

/**
 * tsmux_stream_pcr_unref:
 * @stream: a #TsMuxStream
 *
 * Mark the stream as no longer being used as the PCR for some program.
 */
void
tsmux_stream_pcr_unref (TsMuxStream * stream)
{
  g_return_if_fail (stream != NULL);

  stream->pcr_ref--;
}

/**
 * tsmux_stream_is_pcr:
 * @stream: a #TsMuxStream
 *
 * Check if @stream is used as the PCR for some program.
 *
 * Returns: TRUE if the stream is in use as the PCR for some program.
 */
gboolean
tsmux_stream_is_pcr (TsMuxStream *stream)
{
  return stream->pcr_ref != 0;
}

/**
 * tsmux_stream_get_pts:
 * @stream: a #TsMuxStream
 *
 * Return the PTS of the last buffer that has had bytes written and
 * which _had_ a PTS in @stream.
 *
 * Returns: the PTS of the last buffer in @stream.
 */
guint64
tsmux_stream_get_pts (TsMuxStream * stream)
{
  g_return_val_if_fail (stream != NULL, -1);

  return stream->last_pts;
}
