/* SoX Resampler Library      Copyright (c) 2007-18 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

/* Wrapper mostly compatible with `libsamplerate'. */

#include <assert.h>
#include <stdlib.h>
#include "soxr.h"
#include "soxr-lsr.h"
#include "rint.h"



/* libsamplerate hands its callback (void *, float * *) and returns long;
 * soxr wants (void *, soxr_in_t *, size_t) returning size_t.  The two are
 * not compatible function types, so keep the caller-supplied one beside
 * the resampler and reach it from a correctly typed shim.  */

struct src_state {
  soxr_t         soxr;
  src_callback_t fn;
  void *         fn_state;
};



static size_t lsr_input_fn(void * state, soxr_in_t * data, size_t len)
{
  SRC_STATE * p = state;
  SRC_SAMPLE * buf = 0;
  long done = p->fn(p->fn_state, &buf);

  (void)len;                 /* Not part of the libsamplerate contract. */

  /* A libsamplerate callback signals end-of-input by returning 0 and leaving
   * the pointer alone; soxr says the same by leaving *data at the non-null
   * value it presets.  Writing to it unconditionally would turn that into
   * "input function reported failure".  */
  if (done > 0)
    *data = buf;

  return done > 0? (size_t)done : 0;
}



SRC_STATE *src_new(SRC_SRCTYPE id, int channels, SRC_ERROR * error)
{
  return src_callback_new(0, id, channels, error, 0);
}



SRC_ERROR src_process(SRC_STATE *p, SRC_DATA * io)
{
  size_t idone , odone;

  if (!p || !io) return -1;

  soxr_set_error(p->soxr,
      soxr_set_io_ratio(p->soxr, 1/io->src_ratio, (size_t)io->output_frames));

  soxr_process(p->soxr, io->data_in,                            /* hack: */
      (size_t)(io->end_of_input? ~io->input_frames : io->input_frames),
      &idone, io->data_out, (size_t)io->output_frames, &odone);

  io->input_frames_used = (long)idone, io->output_frames_gen = (long)odone;
  return -!!soxr_error(p->soxr);
}



SRC_ERROR src_set_ratio(SRC_STATE * p, double oi_ratio)
{
  return p? -!!soxr_set_io_ratio(p->soxr, 1/oi_ratio, 0) : -1;
}



SRC_ERROR src_reset(SRC_STATE * p)
{
  return p? -!!soxr_clear(p->soxr) : -1;
}



SRC_ERROR src_error(SRC_STATE * p)
{
  return p? -!!soxr_error(p->soxr) : -1;
}



SRC_STATE * src_delete(SRC_STATE * p)
{
  if (p)
    soxr_delete(p->soxr), free(p);
  return 0;
}



SRC_STATE *src_callback_new(src_callback_t fn,
    SRC_SRCTYPE id, int channels, SRC_ERROR * error0, void * p)
{
  soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_LSR0Q + (unsigned)id, 0);
  char const * e = getenv("SOXR_LSR_NUM_THREADS");
  soxr_runtime_spec_t r_spec = soxr_runtime_spec(!(e && atoi(e) != 1));
  soxr_error_t error = "malloc failed";
  SRC_STATE * state = calloc(1, sizeof(*state));

  assert (channels > 0);

  if (state) {
    state->fn = fn, state->fn_state = p;
    state->soxr =
        soxr_create(0, 0, (unsigned)channels, &error, 0, &q_spec, &r_spec);

    if (!state->soxr)
      free(state), state = 0;
    else if (fn)
      error = soxr_set_input_fn(state->soxr, lsr_input_fn, state, 0);
  }

  if (error0)
    *error0 = -!!error;

  return state;
}



long src_callback_read(SRC_STATE *p, double oi_ratio, long olen, float * obuf)
{
  if (!p || olen < 0) return -1;

  soxr_set_error(p->soxr,
      soxr_set_io_ratio(p->soxr, 1/oi_ratio, (size_t)olen));
  return (long)soxr_output(p->soxr, obuf, (size_t)olen);
}



SRC_ERROR src_simple(SRC_DATA * io, SRC_SRCTYPE id, int channels)
{
  size_t idone, odone;
  soxr_error_t error;
  soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_LSR0Q + (unsigned)id, 0);
  char const * e = getenv("SOXR_LSR_NUM_THREADS");
  soxr_runtime_spec_t r_spec = soxr_runtime_spec(!(e && atoi(e) != 1));

  if (!io || channels<=0 || io->input_frames<0 || io->output_frames<0) return-1;

  error = soxr_oneshot(1, io->src_ratio, (unsigned)channels, io->data_in,
      (size_t)io->input_frames, &idone, io->data_out, (size_t)io->output_frames,
      &odone, 0, &q_spec, &r_spec);

  io->input_frames_used = (long)idone, io->output_frames_gen = (long)odone;

  return -!!error;
}



char const * src_get_name(SRC_SRCTYPE id)
{
  static char const * const names[] = {
    "LSR best sinc", "LSR medium sinc", "LSR fastest sinc",
    "LSR ZOH", "LSR linear", "SoX VHQ"};

  return (unsigned)id < 5u + !getenv("SOXR_LSR_STRICT")? names[id] : 0;
}



char const * src_get_description(SRC_SRCTYPE id)
{
  return src_get_name(id);
}



char const * src_get_version(void)
{
  return soxr_version();
}



char const * src_strerror(SRC_ERROR error)
{
  return error == 1? "Placeholder." : error ? "soxr error" : soxr_strerror(0);
}



int src_is_valid_ratio(double oi_ratio)
{
  return getenv("SOXR_LSR_STRICT")?
    oi_ratio >= 1./256 && oi_ratio <= 256 : oi_ratio > 0;
}



void src_short_to_float_array(short const * src, float * dest, int len)
{
  assert (src && dest);

  while (len--) dest[len] = (float)(src[len] * (1 / (1. + SHRT_MAX)));
}



void src_float_to_short_array(float const * src, short * dest, int len)
{
  double d, N = 1. + SHRT_MAX;
  assert (src && dest);

  while (len--) d = src[len] * N, dest[len] =
    (short)(d > N - 1? (short)(N - 1) : d < -N? (short)-N : rint16(d));
}



void src_int_to_float_array(int const * src, float * dest, int len)
{
  assert (src && dest);
  while (len--) dest[len] = (float)(src[len] * (1 / (32768. * 65536.)));
}



void src_float_to_int_array(float const * src, int * dest, int len)
{
  double d, N = 32768. * 65536.; /* N.B. int32, not int! (Also above fn.) */
  assert (src && dest);

  while (len--) d = src[len] * N, dest[len] =
    d >= N - 1? (int)(N - 1) : d < -N? (int)(-N) : rint32(d);
}
