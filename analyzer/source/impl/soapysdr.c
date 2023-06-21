/*

  Copyright (C) 2023 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#include "soapysdr.h"
#include <analyzer/source.h>
#include <sys/time.h>

#ifdef _SU_SINGLE_PRECISION
#  define SUSCAN_SOAPY_SAMPFMT SOAPY_SDR_CF32
#else
#  define SUSCAN_SOAPY_SAMPFMT SOAPY_SDR_CF64
#endif

SUPRIVATE SoapySDRArgInfo *
suscan_source_soapysdr_find_setting(
  const struct suscan_source_soapysdr *source,
  const char *name)
{
  size_t i;

  for (i = 0; i < source->settings_count; ++i) {
    if (strcmp(source->settings[i].key, name) == 0)
      return source->settings + i;
  }

  return NULL;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_init_sdr(struct suscan_source_soapysdr *self)
{
  suscan_source_config_t *config = self->config;
  unsigned int i;
  char *antenna = NULL;
  const char *key, *desc;
  SoapySDRArgInfo *arg;
  SUBOOL ok = SU_FALSE;

  if ((self->sdr = SoapySDRDevice_make(config->soapy_args)) == NULL) {
    SU_ERROR("Failed to open SDR device: %s\n", SoapySDRDevice_lastError());
    goto done;
  }

  if (self->config->antenna != NULL)
    if (SoapySDRDevice_setAntenna(
        self->sdr,
        SOAPY_SDR_RX,
        config->channel,
        config->antenna) != 0) {
      SU_ERROR("Failed to set SDR antenna: %s\n", SoapySDRDevice_lastError());
      goto done;
    }

  /* Disable AGC to prevent eccentric receivers from ignoring gain settings */
  if (SoapySDRDevice_setGainMode(self->sdr, SOAPY_SDR_RX, 0, false) != 0) {
    SU_ERROR("Failed to disable AGC. This is most likely a driver issue.\n");
    goto done;
  }

  for (i = 0; i < config->gain_count; ++i)
    if (SoapySDRDevice_setGainElement(
        self->sdr,
        SOAPY_SDR_RX,
        config->channel,
        config->gain_list[i]->desc->name,
        config->gain_list[i]->val) != 0)
      SU_WARNING(
          "Failed to set gain `%s' to %gdB, ignoring silently\n",
          config->gain_list[i]->desc->name,
          config->gain_list[i]->val);


  if (SoapySDRDevice_setFrequency(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel,
      config->freq - config->lnb_freq,
      NULL) != 0) {
    SU_ERROR("Failed to set SDR frequency: %s\n", SoapySDRDevice_lastError());
    goto done;
  }

  if (SoapySDRDevice_setBandwidth(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel,
      config->bandwidth) != 0) {
    SU_ERROR("Failed to set SDR IF bandwidth: %s\n", SoapySDRDevice_lastError());
    goto done;
  }

#if SOAPY_SDR_API_VERSION >= 0x00060000
  if (SoapySDRDevice_setFrequencyCorrection(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel,
      config->ppm) != 0) {
    SU_ERROR(
        "Failed to set SDR frequency correction: %s\n",
        SoapySDRDevice_lastError());
    goto done;
  }
#else
  SU_WARNING(
      "SoapySDR "
      SOAPY_SDR_ABI_VERSION
      " does not support frequency correction\n");
#endif /* SOAPY_SDR_API_VERSION >= 0x00060000 */

  if (SoapySDRDevice_setSampleRate(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel,
      config->samp_rate) != 0) {
    SU_ERROR("Failed to set sample rate: %s\n", SoapySDRDevice_lastError());
    goto done;
  }

  /* TODO: Implement IQ balance*/
  self->have_dc = SoapySDRDevice_hasDCOffsetMode(
      self->sdr,
      SOAPY_SDR_RX,
      config->channel);

  if (self->have_dc) {
    if (SoapySDRDevice_setDCOffsetMode(
        self->sdr,
        SOAPY_SDR_RX,
        config->channel,
        config->dc_remove) != 0) {
      SU_ERROR(
          "Failed to set DC offset correction: %s\n",
          SoapySDRDevice_lastError());
      goto done;
    }
  }

  /* All set: open SoapySDR stream */
  self->chan_array[0] = config->channel;

#if SOAPY_SDR_API_VERSION < 0x00080000
  if (SoapySDRDevice_setupStream(
      self->sdr,
      &self->rx_stream,
      SOAPY_SDR_RX,
      SUSCAN_SOAPY_SAMPFMT,
      self->chan_array,
      1,
      NULL) != 0) {
#else
  if ((self->rx_stream = SoapySDRDevice_setupStream(
      self->sdr,
      SOAPY_SDR_RX,
      SUSCAN_SOAPY_SAMPFMT,
      self->chan_array,
      1,
      NULL)) == NULL) {
#endif
    SU_ERROR(
        "Failed to open RX stream on SDR device: %s\n",
        SoapySDRDevice_lastError());
    goto done;
  }

  self->settings = SoapySDRDevice_getSettingInfo(self->sdr, &self->settings_count);
  
  if (self->settings_count != 0 && self->settings == NULL) {
    SU_ERROR(
        "Failed to retrieve device settings: %s\n",
        SoapySDRDevice_lastError());
    goto done;
  }

  for (i = 0; i < config->soapy_args->size; ++i) {
    if (strncmp(
      config->soapy_args->keys[i],
      SUSCAN_SOURCE_SETTING_PREFIX,
      SUSCAN_SOURCE_SETTING_PFXLEN) == 0) {

      key = config->soapy_args->keys[i] + SUSCAN_SOURCE_SETTING_PFXLEN;
      arg = suscan_source_soapysdr_find_setting(self, key);

      if (arg != NULL) {
        desc = arg->description == NULL ? arg->key : arg->description;
        SU_INFO(
          "Device setting `%s': set to %s\n",
          desc,
          config->soapy_args->vals[i]);
      } else {
        SU_WARNING(
          "Device setting `%s': not supported by device. Setting anyways.\n",
          key);
      }

      SoapySDRDevice_writeSetting(
        self->sdr,
        key,
        config->soapy_args->vals[i]);
    }
  }

  self->mtu = SoapySDRDevice_getStreamMTU(self->sdr, self->rx_stream);
  self->samp_rate = SoapySDRDevice_getSampleRate(self->sdr, SOAPY_SDR_RX, 0);

  if ((antenna = SoapySDRDevice_getAntenna(
    self->sdr,
    SOAPY_SDR_RX,
    0)) != NULL) {
    (void) suscan_source_config_set_antenna(config, antenna);
    free(antenna);
  }

  ok = SU_TRUE;

done:
  return ok;
}

/****************************** Implementation ********************************/
SUPRIVATE void
suscan_source_soapysdr_close(void *ptr)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) ptr;

  if (self->rx_stream != NULL)
    SoapySDRDevice_closeStream(self->sdr, self->rx_stream);

  if (self->settings != NULL)
    SoapySDRArgInfoList_clear(self->settings, self->settings_count);

  if (self->sdr != NULL)
    SoapySDRDevice_unmake(self->sdr);
  
  free(self);
}

SUPRIVATE void *
suscan_source_soapysdr_open(
  suscan_source_t *source,
  suscan_source_config_t *config,
  struct suscan_source_info *info)
{
  struct suscan_source_soapysdr *new = NULL;

  SU_ALLOCATE_FAIL(new, struct suscan_source_soapysdr);

  new->config = config;

  SU_TRY_FAIL(suscan_source_soapysdr_init_sdr(new));
  
  /* Initialize source info */

  info->permissions         = SUSCAN_ANALYZER_ALL_SDR_PERMISSIONS;
  
  if (!new->have_dc)
    info->permissions &= ~SUSCAN_ANALYZER_PERM_SET_DC_REMOVE;

  info->mtu                 = new->mtu;
  info->source_samp_rate    = new->samp_rate;
  info->effective_samp_rate = new->samp_rate;
  info->measured_samp_rate  = new->samp_rate;

  gettimeofday(&info->source_start, NULL);

  return new;

fail:
  if (new != NULL)
    suscan_source_soapysdr_close(new);

  return NULL;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_start(void *userdata)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  if (SoapySDRDevice_activateStream(
      self->sdr,
      self->rx_stream,
      0,
      0,
      0) != 0) {
    SU_ERROR("Failed to activate stream: %s\n", SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUSDIFF
suscan_source_soapysdr_read(
  void *userdata,
  SUCOMPLEX *buf,
  SUSCOUNT max)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;
  int result;
  int flags;
  long long timeNs;
  SUBOOL retry;

  do {
    retry = SU_FALSE;
    if (self->force_eos)
      result = 0;
    else
      result = SoapySDRDevice_readStream(
          self->sdr,
          self->rx_stream,
          (void * const*) &buf,
          max,
          &flags,
          &timeNs,
          SUSCAN_SOURCE_DEFAULT_READ_TIMEOUT); /* Setting this to 0 caused extreme CPU usage in MacOS */

    if (result == SOAPY_SDR_TIMEOUT
        || result == SOAPY_SDR_OVERFLOW
        || result == SOAPY_SDR_UNDERFLOW) {
      /* We should use this statuses as quality indicators */
      retry = SU_TRUE;
    }
  } while (retry);

  if (result < 0) {
    SU_ERROR(
        "Failed to read samples from stream: %s (result %d)\n",
        SoapySDR_errToStr(result),
        result);
    return SU_BLOCK_PORT_READ_ERROR_ACQUIRE;
  }

  return result;
}

SUPRIVATE void
suscan_source_soapysdr_get_time(void *userdata, struct timeval *tv)
{
  gettimeofday(tv, NULL);
}


SUPRIVATE SUBOOL
suscan_source_soapysdr_cancel(void *userdata)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  self->force_eos = SU_TRUE;

  if (SoapySDRDevice_deactivateStream(
      self->sdr,
      self->rx_stream,
      0,
      0) != 0) {
    SU_ERROR("Failed to deactivate stream: %s\n", SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_frequency(void *userdata, SUFREQ freq)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set device frequency */
  if (SoapySDRDevice_setFrequency(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      freq,
      NULL) != 0) {
    SU_ERROR("Failed to set SDR frequency: %s\n", SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_gain(void *userdata, const char *name, SUFLOAT gain)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set gain element */
  if (SoapySDRDevice_setGainElement(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      name,
      gain) != 0) {
    SU_ERROR(
        "Failed to set SDR gain `%s': %s\n",
        name,
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_antenna(void *userdata, const char *name)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set antenna */
  if (SoapySDRDevice_setAntenna(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      name) != 0) {
    SU_ERROR(
        "Failed to set SDR antenna `%s': %s\n",
        name,
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_bandwidth(void *userdata, SUFLOAT bw)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set device bandwidth */
  if (SoapySDRDevice_setBandwidth(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      self->config->bandwidth) != 0) {
    SU_ERROR(
        "Failed to set SDR bandwidth: %s\n",
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_ppm(void *userdata, SUFLOAT ppm)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  /* Set ppm correction */
#if SOAPY_SDR_API_VERSION >= 0x00060000
  if (SoapySDRDevice_setFrequencyCorrection(
      self->sdr,
      SOAPY_SDR_RX,
      self->config->channel,
      ppm) != 0) {
    SU_ERROR(
        "Failed to set SDR frequency correction: %s\n",
        SoapySDRDevice_lastError());
    return SU_FALSE;
  }
#else
  SU_WARNING(
      "SoapySDR "
      SOAPY_SDR_ABI_VERSION
      " does not support frequency correction\n");
#endif /* SOAPY_SDR_API_VERSION >= 0x00060000 */

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_dc_remove(void *userdata, SUBOOL remove)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  if (SoapySDRDevice_setDCOffsetMode(
      self->sdr,
      SOAPY_SDR_RX,
      0,
      remove ? true : false)
      != 0) {
    SU_ERROR("Failed to set DC mode\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscan_source_soapysdr_set_agc(void *userdata, SUBOOL set)
{
  struct suscan_source_soapysdr *self = (struct suscan_source_soapysdr *) userdata;

  if (SoapySDRDevice_setGainMode(
      self->sdr,
      SOAPY_SDR_RX,
      0,
      set ? true : false)
      != 0) {
    SU_ERROR("Failed to set AGC\n");
    return SU_FALSE;
  }

  return SU_TRUE;
}

SUPRIVATE struct suscan_source_interface g_soapysdr_source =
{
  .name          = "soapysdr",

  .open          = suscan_source_soapysdr_open,
  .close         = suscan_source_soapysdr_close,
  .start         = suscan_source_soapysdr_start,
  .cancel        = suscan_source_soapysdr_cancel,
  .read          = suscan_source_soapysdr_read,
  .set_frequency = suscan_source_soapysdr_set_frequency,
  .set_gain      = suscan_source_soapysdr_set_gain,
  .set_antenna   = suscan_source_soapysdr_set_antenna,
  .set_bandwidth = suscan_source_soapysdr_set_bandwidth,
  .set_ppm       = suscan_source_soapysdr_set_ppm,
  .set_dc_remove = suscan_source_soapysdr_set_dc_remove,
  .set_agc       = suscan_source_soapysdr_set_agc,
  .get_time      = suscan_source_soapysdr_get_time,

  /* Unser members */
  .seek          = NULL,
  .max_size      = NULL,
  
};

SUBOOL
suscan_source_register_soapysdr(void)
{
  return suscan_source_register(SUSCAN_SOURCE_TYPE_SDR, &g_soapysdr_source);
}
