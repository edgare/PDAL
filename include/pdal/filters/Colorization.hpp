/******************************************************************************
* Copyright (c) 2012, Howard Butler, hobu.inc@gmail.com
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

#ifndef INCLUDED_FILTERS_COLORIZATIONFILTER_HPP
#define INCLUDED_FILTERS_COLORIZATIONFILTER_HPP

#include <pdal/Filter.hpp>
#include <pdal/FilterIterator.hpp>

#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/array.hpp>


namespace pdal
{
class PointBuffer;
namespace gdal
{
class Debug;
}
}

namespace pdal
{
namespace filters
{

namespace colorization
{

typedef boost::shared_ptr<void> DataSourcePtr;

} // colorization

// Provides GDAL-based raster overlay that places output data in
// specified dimensions. It also supports scaling the data by a multiplier
// on a per-dimension basis.
class PDAL_DLL Colorization : public Filter
{
public:
    SET_STAGE_NAME("filters.colorization", "Fetch color information from a GDAL datasource")

    Colorization(Stage& prevStage, const Options&);

    virtual void initialize();
    virtual const Options getDefaultOptions() const;

    bool supportsIterator(StageIteratorType t) const
    {
        if (t == StageIterator_Sequential) return true;

        return false;
    }

    pdal::StageSequentialIterator* createSequentialIterator(PointBuffer& buffer) const;
    pdal::StageRandomIterator* createRandomIterator(PointBuffer&) const
    {
        return NULL;
    }

    void processBuffer(PointBuffer& data) const;

    boost::array<double, 6> getForwardTransform() const
    {
        return m_forward_transform;
    }
    boost::array<double, 6> getInverseTransform() const
    {
        return m_inverse_transform;
    }
    colorization::DataSourcePtr getDataSource() const
    {
        return m_ds;
    }
    std::map<std::string, boost::uint32_t> getBandMap() const
    {
        return m_band_map;
    }
    std::map<std::string, double> getScaleMap() const
    {
        return m_scale_map;
    }

private:
    void collectOptions();

    colorization::DataSourcePtr m_ds;
    boost::shared_ptr<pdal::gdal::Debug> m_gdal_debug;

    std::map<std::string, boost::uint32_t> m_band_map;
    std::map<std::string, double> m_scale_map;
    boost::array<double, 6> m_forward_transform;
    boost::array<double, 6> m_inverse_transform;
    boost::uint32_t m_scale;


    Colorization& operator=(const Colorization&); // not implemented
    Colorization(const Colorization&); // not implemented
};

namespace iterators
{
namespace sequential
{


class PDAL_DLL Colorization : public pdal::FilterSequentialIterator
{
public:
    Colorization(const pdal::filters::Colorization& filter, PointBuffer& buffer);

protected:
    virtual void readBufferBeginImpl(PointBuffer&);

private:
    boost::uint64_t skipImpl(boost::uint64_t);
    boost::uint32_t readBufferImpl(PointBuffer&);
    bool atEndImpl() const;
    double getScaledValue(PointBuffer& data,
                          Dimension const& d,
                          std::size_t pointIndex) const;
    void setScaledValue(PointBuffer& data,
                        double value,
                        Dimension const& d,
                        std::size_t pointIndex) const;
    bool getPixelAndLinePosition(double x,
                                 double y,
                                 boost::array<double, 6> const& inverse,
                                 boost::int32_t& pixel,
                                 boost::int32_t& line,
                                 colorization::DataSourcePtr ds);

    Dimension const* m_dimX;
    Dimension const* m_dimY;

    std::vector<Dimension const*> m_dimensions;
    std::vector<boost::uint32_t> m_bands;
    std::vector<double> m_scales;
    const pdal::filters::Colorization& m_stage;
};


}
} // iterators::sequential


}
} // namespaces

#endif
