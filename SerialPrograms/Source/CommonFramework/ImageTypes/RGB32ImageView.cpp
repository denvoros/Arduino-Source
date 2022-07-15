/*  Image View (RGB 32)
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 */

#include <QImage>
#include "Common/Cpp/Exceptions.h"
#include "RGB32Image.h"
#include "RGB32ImageView.h"

namespace PokemonAutomation{




void ImageViewRGB32::save(const std::string& path) const{
    to_QImage_ref().save(QString::fromStdString(path));
}



ImageViewRGB32::ImageViewRGB32(const QImage& image){
    if (image.isNull()){
        return;
    }
    QImage::Format format = image.format();
    if (format != QImage::Format_ARGB32 && format != QImage::Format_RGB32){
        throw InternalProgramError(nullptr, PA_CURRENT_FUNCTION, "Invalid QImage format.");
    }
    m_width = image.width();
    m_height = image.height();
    m_bytes_per_row = image.bytesPerLine();
    m_ptr = (uint32_t*)image.bits();
}
QImage ImageViewRGB32::to_QImage_ref() const{
    return QImage((const uchar*)m_ptr, (int)m_width, (int)m_height, (int)m_bytes_per_row, QImage::Format_ARGB32);
}
QImage ImageViewRGB32::to_QImage_owning() const{
    return to_QImage_ref().copy();
}
QImage ImageViewRGB32::scaled_to_QImage(size_t width, size_t height) const{
    QImage tmp((const uchar*)m_ptr, (int)m_width, (int)m_height, (int)m_bytes_per_row, QImage::Format_ARGB32);
    if (m_width == width && m_height == height){
        return tmp.copy();
    }
    return tmp.scaled((int)width, (int)height);
}






}
