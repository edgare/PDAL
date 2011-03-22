/******************************************************************************
* Copyright (c) 2011, Michael P. Gerlek (mpg@flaxen.com)
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

#include <cassert>
#include <libpc/exceptions.hpp>
#include <libpc/Color.hpp>
#include <libpc/filters/ColorFilterIterator.hpp>
#include <libpc/filters/ColorFilter.hpp>

namespace libpc { namespace filters {


ColorFilterIterator::ColorFilterIterator(const ColorFilter& filter)
    : libpc::FilterIterator(filter)
    , m_stageAsDerived(filter)
{
    return;
}


boost::uint32_t ColorFilterIterator::readBuffer(PointBuffer& data)
{
    ColorFilter& filter = const_cast<ColorFilter&>(m_stageAsDerived);       // BUG BUG BUG

    getPrevIterator().read(data);

    boost::uint32_t numPoints = data.getNumPoints();

    const SchemaLayout& schemaLayout = data.getSchemaLayout();
    const Schema& schema = schemaLayout.getSchema();

    int fieldIndexR = schema.getDimensionIndex(Dimension::Field_Red);
    int fieldIndexG = schema.getDimensionIndex(Dimension::Field_Green);
    int fieldIndexB = schema.getDimensionIndex(Dimension::Field_Blue);
    int offsetZ = schema.getDimensionIndex(Dimension::Field_Z);

    for (boost::uint32_t pointIndex=0; pointIndex<numPoints; pointIndex++)
    {
        float z = data.getField<float>(pointIndex, offsetZ);
        boost::uint8_t red, green, blue;
        filter.getColor(z, red, green, blue);

        // now we store the 3 u8's in the point data...
        data.setField<boost::uint8_t>(pointIndex, fieldIndexR, red);
        data.setField<boost::uint8_t>(pointIndex, fieldIndexG, green);
        data.setField<boost::uint8_t>(pointIndex, fieldIndexB, blue);
        data.setNumPoints(pointIndex+1);

    }

    return numPoints;
}


void ColorFilterIterator::seekToPoint(boost::uint64_t index)
{
    setCurrentPointIndex(index);
    getPrevIterator().seekToPoint(index);
}

} } // namespaces