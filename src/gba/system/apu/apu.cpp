/**
  * Copyright (C) 2019 fleroviux (Frederic Meyer)
  *
  * This file is part of NanoboyAdvance.
  *
  * NanoboyAdvance is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * NanoboyAdvance is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with NanoboyAdvance. If not, see <http://www.gnu.org/licenses/>.
  */

#include <cmath>

#include "apu.hpp"
#include "../cpu.hpp"

#include <cstdio>

using namespace GameBoyAdvance;

void AudioCallback(APU* apu, std::int16_t* stream, int byte_len) {
  int samples = byte_len/sizeof(std::int16_t)/2;
  int available = apu->buffer->Available();
  
//  while (available-- > 2*samples) {
//    apu->buffer->Read();
//  }
  
  if (available >= 2*samples) {
    for (int x = 0; x < samples; x++) {
      auto sample = apu->buffer->Read() * 32767.0;
      
      stream[x*2+0] = std::int16_t(std::round(sample.left));
      stream[x*2+1] = std::int16_t(std::round(sample.right));
    }
  } else {
    int y = 0;
    
    for (int x = 0; x < samples; x++) {
      auto sample = apu->buffer->Peek(y) * 32767.0;
    
      if (++y == available) y = 0;
      
      stream[x*2+0] = std::int16_t(std::round(sample.left));
      stream[x*2+1] = std::int16_t(std::round(sample.right));
    }
  }
}

APU::APU(CPU* cpu) 
  : cpu(cpu)
  , psg1(cpu->scheduler)
  , psg2(cpu->scheduler)
  , psg3(cpu->scheduler)
  , buffer(new DSP::StereoRingBuffer<float>(16384, true))
  , resampler(new DSP::SincStereoResampler<float, 32>(buffer))
{ }

void APU::Reset() {
  mmio.fifo[0].Reset();
  mmio.fifo[1].Reset();
  mmio.soundcnt.Reset();
  mmio.bias.Reset();
  
  resolution_old = 0;
  event.countdown = 512 >> mmio.bias.resolution;
  
  psg1.Reset();
  psg2.Reset();
  psg3.Reset();
  //psg4.Reset();
  
  dump = fopen("audio.raw", "wb");
  
  auto audio_dev = cpu->config->audio_dev;
  
  /* TODO: check return value for failure. */
  audio_dev->Close();
  audio_dev->Open(this, (AudioDevice::Callback)AudioCallback);
  resampler->SetSampleRates(32768 << mmio.bias.resolution, audio_dev->GetSampleRate());
}
  
void APU::LatchFIFO(int id, int times) {
  auto& fifo = mmio.fifo[id];
  
  for (int time = 0; time < times; time++) {
    latch[id] = fifo.Read();
    if (fifo.Count() <= 16) {
      cpu->dma.Request((id == 0) ? DMA::Occasion::FIFO0 : DMA::Occasion::FIFO1);
    }
  }
}

void APU::Tick() {
  if (mmio.bias.resolution != resolution_old) {
    resampler->SetSampleRates(32768 << mmio.bias.resolution, 
      cpu->config->audio_dev->GetSampleRate());
    resolution_old = mmio.bias.resolution;
  }
  
  resampler->Write({ latch[0]/256.0f + (psg1.sample + psg2.sample + psg3.sample)/512.0f, 
                     latch[1]/256.0f + (psg1.sample + psg2.sample + psg3.sample)/512.0f });
  
  event.countdown += 512 >> mmio.bias.resolution;
}