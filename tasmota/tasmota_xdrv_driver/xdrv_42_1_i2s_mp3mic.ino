/*
  xdrv_42_i2s_audio.ino - Audio dac support for Tasmota

  Copyright (C) 2021  Gerhard Mutz and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifdef ESP32
#if defined(USE_SHINE) && ( (defined(USE_I2S_AUDIO) && defined(USE_I2S_MIC)) || defined(USE_M5STACK_CORE2) || defined(ESP32S3_BOX) )

#define MP3_BOUNDARY "e8b8c539-047d-4777-a985-fbba6edff11e"

uint32_t SpeakerMic(uint8_t spkr) {
esp_err_t err = ESP_OK;


  if (spkr == MODE_SPK) {
    if (audio_i2s.mic_port == 0) {
      I2S_Init_0();
      audio_i2s.out->SetGain(((float)(audio_i2s.is2_volume-2)/100.0)*4.0);
      audio_i2s.out->stop();
    }
    return 0;
  }

  // set micro
  if (audio_i2s.mic_port == 0) {
    // close audio out
    if (audio_i2s.out) {
      audio_i2s.out->stop();
      delete audio_i2s.out;
      audio_i2s.out = nullptr;
    }
    i2s_driver_uninstall(audio_i2s.i2s_port);
  }

  // config mic
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER),
      .sample_rate = audio_i2s.mic_rate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
      .communication_format = I2S_COMM_FORMAT_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 2,
      //.dma_buf_len = 128,
      .dma_buf_len = 1024,
      .use_apll = 0, // Use audio PLL
      .tx_desc_auto_clear     = true,
      .fixed_mclk             = 0,
      .mclk_multiple          = I2S_MCLK_MULTIPLE_DEFAULT,  // I2S_MCLK_MULTIPLE_128
      .bits_per_chan          = I2S_BITS_PER_CHAN_16BIT
  };

#ifdef ESP32S3_BOX
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX);
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#endif

#ifdef USE_I2S_MIC
  // mic select to GND
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
#endif

#ifdef USE_M5STACK_CORE2
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
#endif

  if (audio_i2s.mic_channels == 1) {
    i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
  } else {
    i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  }

  err += i2s_driver_install(audio_i2s.mic_port, &i2s_config, 0, NULL);

  i2s_pin_config_t tx_pin_config;

  tx_pin_config.mck_io_num = audio_i2s.mic_mclk;
  tx_pin_config.bck_io_num = audio_i2s.mic_bclk;
  tx_pin_config.ws_io_num = audio_i2s.mic_ws;
  tx_pin_config.data_out_num = audio_i2s.mic_dout;
  tx_pin_config.data_in_num = audio_i2s.mic_din;

  err += i2s_set_pin(audio_i2s.mic_port, &tx_pin_config);

  i2s_channel_t mode = I2S_CHANNEL_MONO;
  if (audio_i2s.mic_channels > 1) {
    mode = I2S_CHANNEL_STEREO;
  }
  err += i2s_set_clk(audio_i2s.mic_port, audio_i2s.mic_rate, I2S_BITS_PER_SAMPLE_16BIT, mode);

  return err;
}


#ifdef USE_SHINE
#include <layer3.h>
#include <types.h>

#define MP3HANDLECLIENT audio_i2s.MP3Server->handleClient();

// micro to mp3 file or stream
void mic_task(void *arg){
  int8_t error = 0;
  uint8_t *ucp;
  int written;
  shine_config_t  config;
  shine_t s = nullptr;
  uint16_t samples_per_pass;
  File mp3_out = (File)nullptr;
  int16_t *buffer = nullptr;
  uint16_t bytesize;
  uint16_t bwritten;
  uint32_t ctime;

  if (!audio_i2s.use_stream) {
    mp3_out = ufsp->open(audio_i2s.mic_path, "w");
    if (!mp3_out) {
      error = 1;
      goto exit;
    }
  } else {
    if (!audio_i2s.stream_active) {
      error = 2;
      audio_i2s.use_stream = 0;
      goto exit;
    }
    audio_i2s.client.flush();
    audio_i2s.client.setTimeout(3);
    audio_i2s.client.print("HTTP/1.1 200 OK\r\n"
    "Content-Type: audio/mpeg;\r\n\r\n");
    MP3HANDLECLIENT
  }

  shine_set_config_mpeg_defaults(&config.mpeg);

  if (audio_i2s.mic_channels == 1) {
    config.mpeg.mode = MONO;
  } else {
    config.mpeg.mode = STEREO;
  }
  config.mpeg.bitr = 128;
  config.wave.samplerate = audio_i2s.mic_rate;
  config.wave.channels = (channels)audio_i2s.mic_channels;

  if (shine_check_config(config.wave.samplerate, config.mpeg.bitr) < 0) {
    error = 3;
    goto exit;
  }

  s = shine_initialise(&config);
  if (!s) {
    error = 4;
    goto exit;
  }

  samples_per_pass = shine_samples_per_pass(s);
  bytesize = samples_per_pass * 2 * audio_i2s.mic_channels;

  buffer = (int16_t*)malloc(bytesize);
  if (!buffer) {
    error = 5;
    goto exit;
  }

  ctime = TasmotaGlobal.uptime;

  while (!audio_i2s.mic_stop) {
      uint32_t bytes_read;
      i2s_read(audio_i2s.mic_port, (char *)buffer, bytesize, &bytes_read, (100 / portTICK_RATE_MS));
      ucp = shine_encode_buffer_interleaved(s, buffer, &written);

      if (!audio_i2s.use_stream) {
        bwritten = mp3_out.write(ucp, written);
        if (bwritten != written) {
          break;
        }
      } else {
        audio_i2s.client.write((const char*)ucp, written);
        MP3HANDLECLIENT
        if (!audio_i2s.client.connected()) {
          break;
        }
      }
      audio_i2s.recdur = TasmotaGlobal.uptime - ctime;
  }

  ucp = shine_flush(s, &written);

  if (!audio_i2s.use_stream) {
    mp3_out.write(ucp, written);
  } else {
    audio_i2s.client.write((const char*)ucp, written);
    MP3HANDLECLIENT
  }


exit:
  if (s) {
    shine_close(s);
  }
  if (mp3_out) {
    mp3_out.close();
  }
  if (buffer) {
    free(buffer);
  }

  if (audio_i2s.use_stream) {
    audio_i2s.client.stop();
    MP3HANDLECLIENT
  }

  SpeakerMic(MODE_SPK);
  audio_i2s.mic_stop = 0;
  audio_i2s.mic_error = error;
  AddLog(LOG_LEVEL_INFO, PSTR("mp3task error: %d"), error);
  audio_i2s.mic_task_h = 0;
  audio_i2s.recdur = 0;
  audio_i2s.stream_active = 0;
  vTaskDelete(NULL);

}

int32_t i2s_record_shine(char *path) {
esp_err_t err = ESP_OK;

  if (audio_i2s.mic_port == 0) {
    if (audio_i2s.decoder || audio_i2s.mp3) return 0;
  }

  err = SpeakerMic(MODE_MIC);
  if (err) {
    if (audio_i2s.mic_port == 0) {
      SpeakerMic(MODE_SPK);
    }
    AddLog(LOG_LEVEL_INFO, PSTR("mic init error: %d"), err);
    return err;
  }

  strlcpy(audio_i2s.mic_path, path, sizeof(audio_i2s.mic_path));

  audio_i2s.mic_stop = 0;

  uint32_t stack = 4096;

  audio_i2s.use_stream = !strcmp(audio_i2s.mic_path, "stream.mp3");

  if (audio_i2s.use_stream) {
    stack = 8000;
  }

  err = xTaskCreatePinnedToCore(mic_task, "MIC", stack, NULL, 3, &audio_i2s.mic_task_h, 1);

  return err;
}

void Cmd_MicRec(void) {

  if (XdrvMailbox.data_len > 0) {
    if (!strncmp(XdrvMailbox.data, "-?", 2)) {
      Response_P("{\"I2SREC-duration\":%d}", audio_i2s.recdur);
    } else {
      i2s_record_shine(XdrvMailbox.data);
      ResponseCmndChar(XdrvMailbox.data);
    }
  } else {
    if (audio_i2s.mic_task_h) {
      // stop task
      audio_i2s.mic_stop = 1;
      while (audio_i2s.mic_stop) {
        delay(1);
      }
      ResponseCmndChar_P(PSTR("Stopped"));
    }
  }

}

#endif // USE_SHINE
#endif // USE_I2S_AUDIO
#endif // ESP32
