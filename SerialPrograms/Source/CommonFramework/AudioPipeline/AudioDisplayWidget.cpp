/*  Audio Display Widget
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */


#include <cfloat>
#include <cmath>
#include <iostream>
#include <QVBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QIODevice>
#include <QBuffer>
#include "Common/Compiler.h"
#include "AudioConstants.h"
#include "AudioInfo.h"
#include "AudioDisplayWidget.h"
#include "AudioThreadController.h"
#include "CommonFramework/Logging/Logger.h"
#include "Kernels/AbsFFT/Kernels_AbsFFT.h"


#ifdef USE_FFTREAL
#include <fftreal_wrapper.h>
#endif

namespace PokemonAutomation{


AudioDisplayWidget::AudioDisplayWidget(QWidget& parent)
     : QWidget(&parent)
     , m_numFreqs(NUM_FFT_SAMPLES/2)
     , m_numFreqWindows(500)
     , m_numFreqVisBlocks(64)
     , m_freqVisBlockBoundaries(m_numFreqVisBlocks+1)
     , m_freqVisBlocks(m_numFreqVisBlocks * m_numFreqWindows)
{
    // We will display frequencies in log scale, so need to convert
    // log scale: 0, 1/m_numFreqVisBlocks, 2/m_numFreqVisBlocks, ..., 1.0
    // to linear scale:
    // The conversion function is: linear_value = (exp(log_value * LOG_MAX) - 1) / 10
    const float LOG_SCALE_MAX = std::log(11.0f);
    
    m_freqVisBlockBoundaries[0] = 0;
    for(size_t i = 1; i < m_numFreqVisBlocks; i++){
        const float logValue = i / (float)m_numFreqVisBlocks;
        float linearValue = (std::exp(logValue * LOG_SCALE_MAX) - 1.f) / 10.f;
        linearValue = std::max(std::min(linearValue, 1.0f), 0.0f);
        m_freqVisBlockBoundaries[i] = std::min(size_t(linearValue * m_numFreqs + 0.5), m_numFreqs);
    }
    m_freqVisBlockBoundaries[m_numFreqVisBlocks] = m_numFreqs;

    for(size_t i = 1; i <= m_numFreqVisBlocks; i++){
        assert(m_numFreqVisBlocks[i-1] < m_numFreqVisBlocks[i]);
    }

    // std::cout << "Freq vis block boundaries: ";
    // for(const auto v : m_freqVisBlockBoundaries){
    //     std::cout << v << " ";
    // }
    // std::cout << std::endl;
}

AudioDisplayWidget::~AudioDisplayWidget(){ clear(); }

void AudioDisplayWidget::clear(){
    if (m_audioThreadController){
        delete m_audioThreadController;
        m_audioThreadController = nullptr;
    }

    m_freqVisBlocks.assign(m_freqVisBlocks.size(), 0.f);
    m_spectrums.clear();
}

void AudioDisplayWidget::close_audio(){
    clear();

    update_size();
}

void AudioDisplayWidget::set_audio(
    Logger& logger, const AudioInfo& inputInfo, const AudioInfo& outputInfo, float outputVolume
){
    clear();

    m_audioThreadController = new AudioThreadController(this, inputInfo, outputInfo, outputVolume);

    update_size();
}

void AudioDisplayWidget::loadFFTOutput(const QVector<float>& fftOutput){
    // std::cout << "T" << QThread::currentThread() << " AudioDisplayWidget::loadFFTOutput() called" << std::endl;

    auto spectrum = std::make_shared<AudioSpectrum>(0, std::vector<float>(fftOutput.begin(), fftOutput.end()));
    {
        std::lock_guard<std::mutex> lock_gd(m_spectrums_lock);
        spectrum->stamp = (m_spectrums.size() > 0) ? m_spectrums.back()->stamp + 1 : 0;
        m_spectrums.push_front(spectrum);
        if (m_spectrums.size() > m_spectrum_history_length){
            m_spectrums.pop_back();
        }
    }

    // For one window, use how many blocks to show all frequencies:
    for(size_t i = 0; i < m_numFreqVisBlocks; i++){
        float mag = 0.0f;
        for(size_t j = m_freqVisBlockBoundaries[i]; j < m_freqVisBlockBoundaries[i+1]; j++){
            mag += fftOutput[j];
        }
        mag /= m_freqVisBlockBoundaries[i+1] - m_freqVisBlockBoundaries[i];
        mag = std::log(mag * 10.0f + 1.0f);
        // TODO: may need to scale based on game audio volume setting
        // Assuming the max freq magnitude we can get is 20.0, so
        // log(20 * 10 + 1.0) = log(201)
        const float max_log = std::log(201.f);
        mag /= max_log;
        // Clamp to [0.0, 1.0]
        mag = std::min(mag, 1.0f);
        mag = std::max(mag, 0.0f);
        m_freqVisBlocks[m_nextFFTWindowIndex*m_numFreqVisBlocks + i] = mag;
    }
    m_nextFFTWindowIndex = (m_nextFFTWindowIndex+1) % m_numFreqWindows;
    // std::cout << "Computed FFT! "  << magSum << std::endl;

    // Tell Qt to repaint the widget in the next drawing phase in the main loop.
    QWidget::update();

    if (m_saveFreqToDisk){
        for(size_t i = 0; i < m_numFreqs; i++){
            m_freqStream << fftOutput[(int)i] << " ";
        }
        m_freqStream << std::endl;
    }
}

// TODO: move this to a common lib folder:
QColor jetColorMap(float v){
    if (v <= 0.f){
        return QColor(0,0,0);
    }
    else if (v < 0.125f){
        return QColor(0, 0, int((0.5f + 4.f * v) * 255.f));
    }
    else if (v < 0.375f){
        return QColor(0, int((v - 0.125f)*1020.f), 255);
    }
    else if (v < 0.625f){
        int c = int((v - 0.375f) * 1020.f);
        return QColor(c, 255, 255-c);
    }
    else if (v < 0.875f){
        return QColor(255, 255 - int((v-0.625f) * 1020.f), 0);
    }
    else if (v <= 1.0){
        return QColor(255 - int((v-0.875)*1020.f), 0, 0);
    }
    else {
        return QColor(255, 255, 255);
    }
}

void AudioDisplayWidget::paintEvent(QPaintEvent* event){
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    const int widgetWidth = this->width();
    const int widgetHeight = this->height();
    // num frequency bars
    // -1 here because we don't show the freq-0 bar
    const size_t numBars = m_numFreqVisBlocks-1;

    switch (m_audioDisplayType){
    case AudioDisplayType::FREQ_BARS:
    {
        const size_t barPlusGapWidth = widgetWidth / numBars;
        const size_t barWidth = 0.8 * barPlusGapWidth;
        const size_t gapWidth = barPlusGapWidth - barWidth;
        const size_t paddingWidth = widgetWidth - numBars * (barWidth + gapWidth);
        const size_t leftPaddingWidth = (paddingWidth + gapWidth) / 2;
        const size_t barHeight = widgetHeight - 2 * gapWidth;

        for (size_t i = 0; i < numBars; i++){
            size_t curWindow = (m_nextFFTWindowIndex + m_numFreqWindows - 1) % m_numFreqWindows;
            // +1 here to skip the freq-0 value
            float value = m_freqVisBlocks[curWindow * m_numFreqVisBlocks + i+1];
            QRect bar = rect();
            bar.setLeft((int)(rect().left() + leftPaddingWidth + (i * (gapWidth + barWidth))));
            bar.setWidth((int)barWidth);
            bar.setTop((int)(rect().top() + gapWidth + (1.0 - value) * barHeight));
            bar.setBottom((int)(rect().bottom() - gapWidth));

            painter.fillRect(bar, jetColorMap(value));
        }
        break;
    }
    case AudioDisplayType::SPECTROGRAM:
    {
        const size_t barHeight = widgetHeight / numBars;
        const size_t barWidth = widgetWidth;
        for (size_t i = 0; i < numBars; i++){
            QLinearGradient colorGradient(0,barHeight/2,widgetWidth, barHeight/2);
            colorGradient.setSpread(QGradient::PadSpread);
            for(size_t j = 0; j < m_numFreqWindows; j++){
                // Start with the oldest window in time:
                size_t curWindow = (m_nextFFTWindowIndex + m_numFreqWindows + j) % m_numFreqWindows;
                // +1 here to skip the freq-0 value
                float value = m_freqVisBlocks[curWindow * m_numFreqVisBlocks + i+1];

                float pos = (float)j/(m_numFreqWindows - 1);
                colorGradient.setColorAt(pos, jetColorMap(value));
            }

            QRect bar = rect();
            bar.setLeft(rect().left());
            bar.setWidth((int)barWidth);
            bar.setTop((int)(rect().top() + i * barHeight));
            bar.setBottom((int)(rect().top() + (i+1) * barHeight));

            painter.fillRect(bar, QBrush(colorGradient));
        }
    }
    default:
        break;
    }
}

void AudioDisplayWidget::setAudioDisplayType(AudioDisplayType type){
    m_audioDisplayType = type;
    update_size();
}

void AudioDisplayWidget::update_size(){
    int height = m_audioDisplayType == AudioDisplayType::NO_DISPLAY ? 0 : this->width() / 6;
    this->setFixedHeight(height);
}

void AudioDisplayWidget::resizeEvent(QResizeEvent* event){
    QWidget::resizeEvent(event);
    // std::cout << "Audio widget size: " << this->width() << " x " << this->height() << std::endl;

    int width = this->width();

    //  Safeguard against a resizing loop where the UI bounces between larger
    //  height with scroll bar and lower height with no scroll bar.
    auto iter = m_recent_widths.find(width);
    if (iter != m_recent_widths.end() && std::abs(width - event->oldSize().width()) < 50){
//        cout << "Supressing potential infinite resizing loop." << endl;
        return;
    }

    m_width_history.push_back(width);
    m_recent_widths.insert(width);
    if (m_width_history.size() > 10){
        m_recent_widths.erase(m_width_history[0]);
        m_width_history.pop_front();
    }

    update_size();
}


void AudioDisplayWidget::spectrums_since(size_t startingStamp, std::vector<std::shared_ptr<AudioSpectrum>>& spectrums){
    for(const auto& ptr : m_spectrums){
        if (ptr->stamp >= startingStamp){
            spectrums.push_back(ptr);
        } else{
            break;
        }
    }
}

void AudioDisplayWidget::spectrums_latest(size_t numLatestSpectrums, std::vector<std::shared_ptr<AudioSpectrum>>& spectrums){
    size_t i = 0;
    for(const auto& ptr : m_spectrums){
        if (i == numLatestSpectrums){
            break;
        }
        spectrums.push_back(ptr);
        i++;
    }
}


void AudioDisplayWidget::saveAudioFrequenciesToDisk(bool enable){
    if (enable){
        if (m_saveFreqToDisk == false){
            m_saveFreqToDisk = enable;
            m_freqStream.open("./frequencies.txt");
        }
    } else if (m_saveFreqToDisk){
        m_saveFreqToDisk = enable;
        m_freqStream.close();
    }
}

}
