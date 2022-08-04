/*  Audio Source Reader
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include <QIODevice>
#include "Common/Cpp/Exceptions.h"
#include "Common/Cpp/AlignedVector.tpp"
#include "Kernels/AudioStreamConversion/AudioStreamConversion.h"
#include "Kernels/AbsFFT/Kernels_AbsFFT.h"
#include "AudioConstants.h"
#include "AudioIODevice.h"
#include "AudioStream.h"

#include <iostream>
using std::cout;
using std::endl;

namespace PokemonAutomation{


//  Audio buffer size (measured in frames).
const size_t AUDIO_BUFFER_SIZE = 4096;



size_t sample_size(AudioSampleFormat format){
    switch (format){
    case AudioSampleFormat::UINT8:
        return sizeof(uint8_t);
    case AudioSampleFormat::SINT16:
        return sizeof(int16_t);
    case AudioSampleFormat::SINT32:
        return sizeof(int32_t);
    case AudioSampleFormat::FLOAT32:
        return sizeof(float);
    default:
        throw InternalProgramError(nullptr, PA_CURRENT_FUNCTION, "Invalid AudioSampleFormat: " + std::to_string((size_t)format));
    }
}



void AudioStreamToFloat::add_listener(AudioFloatStreamListener& listener){
    if (listener.samples_per_frame != m_samples_per_frame){
        throw InternalProgramError(nullptr, PA_CURRENT_FUNCTION, "Mismatching frame size.");
    }
    m_listeners.insert(&listener);
}
void AudioStreamToFloat::remove_listener(AudioFloatStreamListener& listener){
    m_listeners.erase(&listener);
}

AudioStreamToFloat::~AudioStreamToFloat(){
    MisalignedStreamConverter::remove_listener(*this);
}
AudioStreamToFloat::AudioStreamToFloat(
    AudioSampleFormat input_format,
    size_t samples_per_frame,
    bool reverse_channels
)
    : MisalignedStreamConverter(
        sample_size(input_format) * samples_per_frame,
        sizeof(float) * samples_per_frame,
        AUDIO_BUFFER_SIZE
    )
    , StreamListener(sizeof(float) * samples_per_frame)
    , m_format(input_format)
    , m_samples_per_frame(samples_per_frame)
    , m_reverse_channels(reverse_channels)
    , m_sample_size(sample_size(input_format))
    , m_frame_size(m_sample_size * samples_per_frame)
{
    if (samples_per_frame == 0){
        throw InternalProgramError(nullptr, PA_CURRENT_FUNCTION, "Must have at least one sample.");
    }
    if (reverse_channels && samples_per_frame != 2){
        throw InternalProgramError(nullptr, PA_CURRENT_FUNCTION, "Reverse channels only works with 2 samples/frame.");
    }
    MisalignedStreamConverter::add_listener(*this);
}
void AudioStreamToFloat::on_objects(const void* data, size_t objects){
    for (AudioFloatStreamListener* listener : m_listeners){
        listener->on_samples((const float*)data, objects);
    }
}
void AudioStreamToFloat::convert(void* out, const void* in, size_t count){
    switch (m_format){
    case AudioSampleFormat::UINT8:
        Kernels::AudioStreamConversion::convert_audio_uint8_to_float((float*)out, (const uint8_t*)in, count * m_samples_per_frame);
        break;
    case AudioSampleFormat::SINT16:
        Kernels::AudioStreamConversion::convert_audio_sint16_to_float((float*)out, (const int16_t*)in, count * m_samples_per_frame);
        break;
    case AudioSampleFormat::SINT32:
        Kernels::AudioStreamConversion::convert_audio_sint32_to_float((float*)out, (const int32_t*)in, count * m_samples_per_frame);
        break;
    case AudioSampleFormat::FLOAT32:
        memcpy(out, in, count * m_frame_size);
        break;
    case AudioSampleFormat::INVALID:
        break;
    }
    if (m_reverse_channels){
        float* ptr = (float*)out;
        for (size_t c = 0; c < count; c++){
            float r0 = ptr[2*c + 0];
            float r1 = ptr[2*c + 1];
            ptr[2*c + 0] = r1;
            ptr[2*c + 1] = r0;
        }
    }
}




void AudioFloatToStream::add_listener(StreamListener& listener){
    if (listener.object_size != m_frame_size){
        throw InternalProgramError(nullptr, PA_CURRENT_FUNCTION, "Mismatching frame size.");
    }
    m_listeners.insert(&listener);
}
void AudioFloatToStream::remove_listener(StreamListener& listener){
    m_listeners.erase(&listener);
}

AudioFloatToStream::AudioFloatToStream(QIODevice* audio_sink, AudioSampleFormat output_format, size_t samples_per_frame)
    : AudioFloatStreamListener(samples_per_frame)
    , m_audio_sink(audio_sink)
    , m_format(output_format)
    , m_samples_per_frame(samples_per_frame)
    , m_sample_size(sample_size(output_format))
    , m_frame_size(m_sample_size * samples_per_frame)
    , m_buffer_size(AUDIO_BUFFER_SIZE)
{
    switch (output_format){
    case AudioSampleFormat::INVALID:
    case AudioSampleFormat::FLOAT32:
        break;
    default:
        m_buffer = AlignedVector<char>(m_frame_size * m_buffer_size);
    }
}
AudioFloatToStream::~AudioFloatToStream(){}
void AudioFloatToStream::on_samples(const float* data, size_t frames){
    if (m_format == AudioSampleFormat::INVALID){
        return;
    }
    if (m_format == AudioSampleFormat::FLOAT32){
        if (m_audio_sink){
            m_audio_sink->write((const char*)data, frames * m_frame_size);
        }
        for (StreamListener* listener : m_listeners){
            listener->on_objects(data, frames);
        }
        return;
    }

    while (frames > 0){
        size_t block = std::min(frames, m_buffer_size);
        switch (m_format){
        case AudioSampleFormat::UINT8:
            Kernels::AudioStreamConversion::convert_audio_float_to_uint8((uint8_t*)m_buffer.data(), data, block * m_samples_per_frame);
            break;
        case AudioSampleFormat::SINT16:
            Kernels::AudioStreamConversion::convert_audio_float_to_sint16((int16_t*)m_buffer.data(), data, block * m_samples_per_frame);
            break;
        case AudioSampleFormat::SINT32:
            Kernels::AudioStreamConversion::convert_audio_float_to_sint32((int32_t*)m_buffer.data(), data, block * m_samples_per_frame);
            break;
        case AudioSampleFormat::FLOAT32:
            memcpy(m_buffer.data(), data, block * m_frame_size);
            break;
        default:
            return;
        }
        if (m_audio_sink){
            m_audio_sink->write(m_buffer.data(), block * m_frame_size);
        }
        for (StreamListener* listener : m_listeners){
            listener->on_objects(m_buffer.data(), block);
        }
        data = (const float*)((const char*)data + block * m_frame_size);
        frames -= block;
    }
}





FFTRunner::FFTRunner(
    AudioIODevice& device, size_t sample_rate,
    size_t samples_per_frame, bool average_pairs
)
    : AudioFloatStreamListener(samples_per_frame)
    , m_device(device)
    , m_sample_rate(sample_rate)
    , m_average(average_pairs)
    , m_fft_sample_size(average_pairs ? 2 : 1)
    , m_buffer(NUM_FFT_SAMPLES)
    , m_buffered(NUM_FFT_SAMPLES)
    , m_fft_input(NUM_FFT_SAMPLES)
{
    if (samples_per_frame == 0 || samples_per_frame > 2){
        throw InternalProgramError(nullptr, PA_CURRENT_FUNCTION, "Channels must be 1 or 2.");
    }
    memset(m_buffer.data(), 0, m_buffer.size() * sizeof(float));
}
FFTRunner::~FFTRunner(){}
void FFTRunner::on_samples(const float* data, size_t frames){
//    cout << "objects = " << objects << endl;
    const float* ptr = data;
    while (frames > 0){
        //  Figure out how much space we can write contiguously.
        size_t block = m_buffer.size() - std::max(m_buffered, m_end);

        //  Don't write more than we have.
        block = std::min(block, frames);

//        cout << "block = " << block << endl;

        //  Write it.
        convert(&m_buffer[m_end], ptr, block);
        m_buffered += block;
        m_end += block;
        if (m_end == m_buffer.size()){
            m_end = 0;
        }
        ptr += block * m_fft_sample_size;
        frames -= block;

        //  Buffer is full. Time to run FFT!
        if (m_buffered == m_buffer.size()){
//            cout << "run_fft()" << endl;
            run_fft();
            drop_from_front(FFT_SLIDING_WINDOW_STEP);
        }
    }
}
void FFTRunner::convert(float* fft_input, const float* audio_stream, size_t frames){
    if (!m_average){
        memcpy(fft_input, audio_stream, frames * sizeof(float));
        return;
    }
    for (size_t c = 0; c < frames; c++){
        fft_input[c] = (audio_stream[2*c + 0] + audio_stream[2*c + 1]) * 0.5;
    }
}
void FFTRunner::run_fft(){
    float* ptr = m_fft_input.data();
    size_t remaining = NUM_FFT_SAMPLES;
    size_t index = m_start;
    while (remaining > 0){
        size_t block = std::min(remaining, m_buffer.size() - index);
        memcpy(ptr, &m_buffer[index], block * sizeof(float));
        ptr += block;
        remaining -= block;
        index += block;
        if (index == m_buffer.size()){
            index = 0;
        }
    }
    std::shared_ptr<AlignedVector<float>> out = std::make_unique<AlignedVector<float>>(NUM_FFT_SAMPLES / 2);
    Kernels::AbsFFT::fft_abs(FFT_LENGTH_POWER_OF_TWO, out->data(), m_fft_input.data());
    emit m_device.fftOutputReady(m_sample_rate, std::move(out));
}
void FFTRunner::drop_from_front(size_t frames){
    if (frames >= m_buffered){
        m_buffered = 0;
        m_start = 0;
        m_end = 0;
        return;
    }
    m_buffered -= frames;
    m_start += frames;
    while (m_start >= m_buffer.size()){
        m_start -= m_buffer.size();
    }
}





}
