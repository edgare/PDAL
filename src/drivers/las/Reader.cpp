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

#include <pdal/drivers/las/Reader.hpp>
#include <pdal/drivers/las/Support.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#ifdef PDAL_HAVE_LASZIP
#include <laszip/lasunzipper.hpp>
#endif

#include <pdal/FileUtils.hpp>
#include <pdal/drivers/las/Header.hpp>
#include <pdal/drivers/las/VariableLengthRecord.hpp>
#include "LasHeaderReader.hpp"
#include <pdal/PointBuffer.hpp>
#include <pdal/Metadata.hpp>
#include "ZipPoint.hpp"

#ifdef PDAL_HAVE_GDAL
#include "gdal.h"
#include "cpl_vsi.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#endif

#if defined(EQUAL) && defined(PDAL_PLATFORM_WIN32)
#undef EQUAL
#define EQUAL(a,b) (_stricmp(a,b)==0)
#endif

namespace pdal
{
namespace drivers
{
namespace las
{


Reader::Reader(const Options& options)
    : ReaderBase(options)
    , m_streamFactory(new FilenameStreamFactory(options.getValueOrThrow<std::string>("filename")))
    , m_ownsStreamFactory(true)
{
    addDefaultDimensions();
    return;
}


Reader::Reader(const std::string& filename)
    : ReaderBase(Options::none())
    , m_streamFactory(new FilenameStreamFactory(filename))
    , m_ownsStreamFactory(true)
{
    addDefaultDimensions();
    return;
}


Reader::Reader(StreamFactory* factory)
    : ReaderBase(Options::none())
    , m_streamFactory(factory)
    , m_ownsStreamFactory(false)
{
    addDefaultDimensions();
    return;
}


Reader::~Reader()
{
    if (m_ownsStreamFactory && m_streamFactory!=NULL)
    {
        delete m_streamFactory;
    }

    return;
}


void Reader::initialize()
{
    pdal::Reader::initialize();

    std::istream& stream = m_streamFactory->allocate();

    LasHeaderReader lasHeaderReader(m_lasHeader, stream);
    lasHeaderReader.read(*this, getSchemaRef());

    this->setBounds(m_lasHeader.getBounds());
    this->setNumPoints(m_lasHeader.GetPointRecordsCount());

    // If the user is already overriding this by setting it on the stage, we'll
    // take their overridden value
    if (getSpatialReference().getWKT(pdal::SpatialReference::eCompoundOK).empty())
    {
        SpatialReference srs;
        m_lasHeader.getVLRs().constructSRS(srs);
        setSpatialReference(srs);
    }

    collectMetadata();

    m_streamFactory->deallocate(stream);

    return;
}


const Options Reader::getDefaultOptions() const
{
    Option option1("filename", "", "file to read from");
    Options options(option1);
    return options;
}


StreamFactory& Reader::getStreamFactory() const
{
    return *m_streamFactory;
}


PointFormat Reader::getPointFormat() const
{
    return m_lasHeader.getPointFormat();
}


boost::uint8_t Reader::getVersionMajor() const
{
    return m_lasHeader.GetVersionMajor();
}


boost::uint8_t Reader::getVersionMinor() const
{
    return m_lasHeader.GetVersionMinor();
}


const std::vector<VariableLengthRecord>& Reader::getVLRs() const
{
    return m_lasHeader.getVLRs().getAll();
}


boost::uint64_t Reader::getPointDataOffset() const
{
    return m_lasHeader.GetDataOffset();
}


bool Reader::isCompressed() const
{
    return m_lasHeader.Compressed();
}


pdal::StageSequentialIterator* Reader::createSequentialIterator(PointBuffer& buffer) const
{
    return new pdal::drivers::las::iterators::sequential::Reader(*this, buffer);
}


pdal::StageRandomIterator* Reader::createRandomIterator(PointBuffer& buffer) const
{
    return new pdal::drivers::las::iterators::random::Reader(*this, buffer);
}


boost::uint32_t Reader::processBuffer(PointBuffer& data,
                                      std::istream& stream,
                                      boost::uint64_t numPointsLeft,
                                      LASunzipper* unzipper,
                                      ZipPoint* zipPoint,
                                      PointDimensions* dimensions) const
{
    // we must not read more points than are left in the file
    const boost::uint64_t numPoints64 = std::min<boost::uint64_t>(data.getCapacity(), numPointsLeft);
    const boost::uint32_t numPoints = (boost::uint32_t)std::min<boost::uint64_t>(numPoints64, std::numeric_limits<boost::uint32_t>::max());

    const LasHeader& lasHeader = getLasHeader();
    const PointFormat pointFormat = lasHeader.getPointFormat();


    const bool hasTime = Support::hasTime(pointFormat);
    const bool hasColor = Support::hasColor(pointFormat);
    const int pointByteCount = Support::getPointDataSize(pointFormat);

    boost::uint8_t* buf = new boost::uint8_t[pointByteCount * numPoints];

    if (!dimensions)
    {
        throw pdal_error("No dimension positions are available!");
    }
    if (zipPoint)
    {
#ifdef PDAL_HAVE_LASZIP
        boost::uint8_t* p = buf;

        bool ok = false;
        for (boost::uint32_t i=0; i<numPoints; i++)
        {
            ok = unzipper->read(zipPoint->m_lz_point);
            if (!ok)
            {
                std::ostringstream oss;
                const char* err = unzipper->get_error();
                if (err==NULL) err="(unknown error)";
                oss << "Error reading compressed point data: " << std::string(err);
                throw pdal_error(oss.str());
            }

            memcpy(p, zipPoint->m_lz_point_data.get(), zipPoint->m_lz_point_size);
            p +=  zipPoint->m_lz_point_size;
        }
#else
        boost::ignore_unused_variable_warning(unzipper);
        throw pdal_error("LASzip is not enabled for this pdal::drivers::las::Reader::processBuffer");
#endif
    }
    else
    {
        Utils::read_n(buf, stream, pointByteCount * numPoints);
    }

    for (boost::uint32_t pointIndex=0; pointIndex<numPoints; pointIndex++)
    {
        boost::uint8_t* p = buf + pointByteCount * pointIndex;

        // always read the base fields
        {
            const boost::int32_t x = Utils::read_field<boost::int32_t>(p);
            const boost::int32_t y = Utils::read_field<boost::int32_t>(p);
            const boost::int32_t z = Utils::read_field<boost::int32_t>(p);
            const boost::uint16_t intensity = Utils::read_field<boost::uint16_t>(p);
            const boost::uint8_t flags = Utils::read_field<boost::uint8_t>(p);
            const boost::uint8_t classification = Utils::read_field<boost::uint8_t>(p);
            const boost::int8_t scanAngleRank = Utils::read_field<boost::int8_t>(p);
            const boost::uint8_t user = Utils::read_field<boost::uint8_t>(p);
            const boost::uint16_t pointSourceId = Utils::read_field<boost::uint16_t>(p);

            const boost::uint8_t returnNum = flags & 0x07;
            const boost::uint8_t numReturns = (flags >> 3) & 0x07;
            const boost::uint8_t scanDirFlag = (flags >> 6) & 0x01;
            const boost::uint8_t flight = (flags >> 7) & 0x01;
            
            if (dimensions->X)
                data.setField<boost::uint32_t>(*dimensions->X, pointIndex, x);

            if (dimensions->Y)
                data.setField<boost::uint32_t>(*dimensions->Y, pointIndex, y);

            if (dimensions->Z)
                data.setField<boost::uint32_t>(*dimensions->Z, pointIndex, z);

            if (dimensions->Intensity)
                data.setField<boost::uint16_t>(*dimensions->Intensity, pointIndex, intensity);
            
            if (dimensions->ReturnNumber)
                data.setField<boost::uint8_t>(*dimensions->ReturnNumber, pointIndex, returnNum);
            
            if (dimensions->NumberOfReturns)
                data.setField<boost::uint8_t>(*dimensions->NumberOfReturns, pointIndex, numReturns);
            
            if (dimensions->ScanDirectionFlag)
                data.setField<boost::uint8_t>(*dimensions->ScanDirectionFlag, pointIndex, scanDirFlag);
            
            if (dimensions->EdgeOfFlightLine)
                data.setField<boost::uint8_t>(*dimensions->EdgeOfFlightLine, pointIndex, flight);
            
            if (dimensions->Classification)
                data.setField<boost::uint8_t>(*dimensions->Classification, pointIndex, classification);
            
            if (dimensions->ScanAngleRank)
                data.setField<boost::int8_t>(*dimensions->ScanAngleRank, pointIndex, scanAngleRank);
            
            if (dimensions->UserData)
                data.setField<boost::uint8_t>(*dimensions->UserData, pointIndex, user);
            
            if (dimensions->PointSourceId)
                data.setField<boost::uint16_t>(*dimensions->PointSourceId, pointIndex, pointSourceId);
        }

        if (hasTime)
        {
            const double time = Utils::read_field<double>(p);
            
            if (dimensions->Time)
                data.setField<double>(*dimensions->Time, pointIndex, time);
        }

        if (hasColor)
        {
            const boost::uint16_t red = Utils::read_field<boost::uint16_t>(p);
            const boost::uint16_t green = Utils::read_field<boost::uint16_t>(p);
            const boost::uint16_t blue = Utils::read_field<boost::uint16_t>(p);
            
            if (dimensions->Red)
                data.setField<boost::uint16_t>(*dimensions->Red, pointIndex, red);
            
            if (dimensions->Green)
                data.setField<boost::uint16_t>(*dimensions->Green, pointIndex, green);
            
            if (dimensions->Blue)
                data.setField<boost::uint16_t>(*dimensions->Blue, pointIndex, blue);
        }

        data.setNumPoints(pointIndex+1);
    }

    delete[] buf;

    data.setSpatialBounds(lasHeader.getBounds());

    return numPoints;
}

void Reader::collectMetadata()
{

    LasHeader const& header = getLasHeader();

    Metadata& metadata = getMetadataRef();

    metadata.addEntry<bool>("compressed",
                            header.Compressed());
    metadata.addEntry<boost::uint32_t>("dataformatid",
                                       static_cast<boost::uint32_t>(header.getPointFormat()));
    metadata.addEntry<boost::uint32_t>("version_major",
                                       static_cast<boost::uint32_t>(header.GetVersionMajor()));
    metadata.addEntry<boost::uint32_t>("version_minor",
                                       static_cast<boost::uint32_t>(header.GetVersionMinor()));
    metadata.addEntry<boost::uint32_t>("filesource_id",
                                       static_cast<boost::uint32_t>(header.GetFileSourceId()));
    metadata.addEntry<boost::uint32_t>("reserved",
                                       static_cast<boost::uint32_t>(header.GetReserved()));
    metadata.addEntry<boost::uuids::uuid>("project_id",
                                          header.GetProjectId());
    metadata.addEntry<std::string>("system_id",
                                   header.GetSystemId(false));
    metadata.addEntry<std::string>("software_id",
                                   header.GetSoftwareId(false));
    metadata.addEntry<boost::uint32_t>("creation_doy",
                                       static_cast<boost::uint32_t>(header.GetCreationDOY()));
    metadata.addEntry<boost::uint32_t>("creation_year",
                                       static_cast<boost::uint32_t>(header.GetCreationYear()));
    metadata.addEntry<boost::uint32_t>("header_size",
                                       static_cast<boost::uint32_t>(header.GetHeaderSize()));
    metadata.addEntry<boost::uint32_t>("dataoffset",
                                       static_cast<boost::uint32_t>(header.GetDataOffset()));
    metadata.addEntry<double>("scale_x",
                              header.GetScaleX());
    metadata.addEntry<double>("scale_y",
                              header.GetScaleY());
    metadata.addEntry<double>("scale_z",
                              header.GetScaleZ());
    metadata.addEntry<double>("offset_x",
                              header.GetOffsetX());
    metadata.addEntry<double>("offset_y",
                              header.GetOffsetY());
    metadata.addEntry<double>("offset_z",
                              header.GetOffsetZ());
    metadata.addEntry<double>("minx",
                              header.GetMinX());
    metadata.addEntry<double>("miny",
                              header.GetMinY());
    metadata.addEntry<double>("minz",
                              header.GetMinZ());
    metadata.addEntry<double>("maxx",
                              header.GetMaxX());
    metadata.addEntry<double>("maxy",
                              header.GetMaxY());
    metadata.addEntry<double>("maxz",
                              header.GetMaxZ());
    metadata.addEntry<boost::uint32_t>("count",
                                       header.GetPointRecordsCount());


    std::vector<VariableLengthRecord> const& vlrs = header.getVLRs().getAll();
    for (std::vector<VariableLengthRecord>::size_type t = 0;
            t < vlrs.size();
            ++t)
    {
        VariableLengthRecord const& v = vlrs[t];

        std::vector<boost::uint8_t> raw_bytes;
        for (std::size_t i = 0 ; i < v.getLength(); ++i)
        {
            raw_bytes.push_back(v.getBytes()[i]);
        }
        pdal::ByteArray bytearray(raw_bytes);

        std::ostringstream name;
        name << "vlr_" << t;
        pdal::metadata::Entry entry(name.str());
        entry.setValue<pdal::ByteArray>(bytearray);
        entry.addAttribute("reserved", boost::lexical_cast<std::string>(v.getReserved()));
        entry.addAttribute("user_id", v.getUserId());
        entry.addAttribute("record_id", boost::lexical_cast<std::string>(v.getRecordId()));
        entry.addAttribute("description", v.getDescription());

        metadata.addEntry(entry);
    }

}

void Reader::addDefaultDimensions()
{
    Dimension x("X", dimension::SignedInteger, 4,
                "X coordinate as a long integer. You must use the scale "
                "and offset information of the header to determine the double value.");
    x.setUUID("2ee118d1-119e-4906-99c3-42934203f872");
    addDefaultDimension(x, getName());

    Dimension y("Y", dimension::SignedInteger, 4,
                "Y coordinate as a long integer. You must use the scale "
                "and offset information of the header to determine the double value.");
    y.setUUID("87707eee-2f30-4979-9987-8ef747e30275");
    addDefaultDimension(y, getName());

    Dimension z("Z", dimension::SignedInteger, 4,
                "x coordinate as a long integer. You must use the scale and "
                "offset information of the header to determine the double value.");
    z.setUUID("e74b5e41-95e6-4cf2-86ad-e3f5a996da5d");
    addDefaultDimension(z, getName());

    Dimension time("Time", dimension::Float, 8,
                   "The GPS Time is the double floating point time tag value at "
                   "which the point was acquired. It is GPS Week Time if the "
                   "Global Encoding low bit is clear and Adjusted Standard GPS "
                   "Time if the Global Encoding low bit is set (see Global Encoding "
                   "in the Public Header Block description).");
    time.setUUID("aec43586-2711-4e59-9df0-65aca78a4ffc");
    addDefaultDimension(time, getName());

    Dimension intensity("Intensity", dimension::UnsignedInteger, 2,
                        "The intensity value is the integer representation of the pulse "
                        "return magnitude. This value is optional and system specific. "
                        "However, it should always be included if available.");
    intensity.setUUID("61e90c9a-42fc-46c7-acd3-20d67bd5626f");
    addDefaultDimension(intensity, getName());

    Dimension return_number("ReturnNumber", dimension::UnsignedInteger, 1,
                            "Return Number: The Return Number is the pulse return number for "
                            "a given output pulse. A given output laser pulse can have many "
                            "returns, and they must be marked in sequence of return. The first "
                            "return will have a Return Number of one, the second a Return "
                            "Number of two, and so on up to five returns.");
    return_number.setUUID("ffe5e5f8-4cec-4560-abf0-448008f7b89e");
    addDefaultDimension(return_number, getName());

    Dimension number_of_returns("NumberOfReturns", dimension::UnsignedInteger, 1,
                                "Number of Returns (for this emitted pulse): The Number of Returns "
                                "is the total number of returns for a given pulse. For example, "
                                "a laser data point may be return two (Return Number) within a "
                                "total number of five returns.");
    number_of_returns.setUUID("7c28bfd4-a9ed-4fb2-b07f-931c076fbaf0");
    addDefaultDimension(number_of_returns, getName());

    Dimension scan_direction("ScanDirectionFlag", dimension::UnsignedInteger, 1,
                             "The Scan Direction Flag denotes the direction at which the "
                             "scanner mirror was traveling at the time of the output pulse. "
                             "A bit value of 1 is a positive scan direction, and a bit value "
                             "of 0 is a negative scan direction (where positive scan direction "
                             "is a scan moving from the left side of the in-track direction to "
                             "the right side and negative the opposite).");
    scan_direction.setUUID("13019a2c-cf88-480d-a995-0162055fe5f9");
    addDefaultDimension(scan_direction, getName());

    Dimension edge("EdgeOfFlightLine", dimension::UnsignedInteger, 1,
                   "The Edge of Flight Line data bit has a value of 1 only when "
                   "the point is at the end of a scan. It is the last point on "
                   "a given scan line before it changes direction.");
    edge.setUUID("108c18f2-5cc0-4669-ae9a-f41eb4006ea5");
    addDefaultDimension(edge, getName());

    Dimension classification("Classification", dimension::UnsignedInteger, 1,
                             "Classification in LAS 1.0 was essentially user defined and optional. "
                             "LAS 1.1 defines a standard set of ASPRS classifications. In addition, "
                             "the field is now mandatory. If a point has never been classified, this "
                             "byte must be set to zero. There are no user defined classes since "
                             "both point format 0 and point format 1 supply 8 bits per point for "
                             "user defined operations. Note that the format for classification is a "
                             "bit encoded field with the lower five bits used for class and the "
                             "three high bits used for flags.");
    classification.setUUID("b4c67de9-cef1-432c-8909-7c751b2a4e0b");
    addDefaultDimension(classification, getName());

    Dimension scan_angle("ScanAngleRank", dimension::SignedInteger, 1,
                         "The Scan Angle Rank is a signed one-byte number with a "
                         "valid range from -90 to +90. The Scan Angle Rank is the "
                         "angle (rounded to the nearest integer in the absolute "
                         "value sense) at which the laser point was output from the "
                         "laser system including the roll of the aircraft. The scan "
                         "angle is within 1 degree of accuracy from +90 to 90 degrees. "
                         "The scan angle is an angle based on 0 degrees being nadir, "
                         "and 90 degrees to the left side of the aircraft in the "
                         "direction of flight.");
    scan_angle.setUUID("aaadaf77-e0c9-4df0-81a7-27060794cd69");
    addDefaultDimension(scan_angle, getName());

    Dimension user_data("UserData", dimension::UnsignedInteger, 1,
                        "This field may be used at the users discretion");
    user_data.setUUID("70eb558e-63d4-4804-b1db-fc2fd716927c");
    addDefaultDimension(user_data, getName());

    Dimension point_source("PointSourceId", dimension::UnsignedInteger, 2,
                           "This value indicates the file from which this point originated. "
                           "Valid values for this field are 1 to 65,535 inclusive with zero "
                           "being used for a special case discussed below. The numerical value "
                           "corresponds to the File Source ID from which this point originated. "
                           "Zero is reserved as a convenience to system implementers. A Point "
                           "Source ID of zero implies that this point originated in this file. "
                           "This implies that processing software should set the Point Source "
                           "ID equal to the File Source ID of the file containing this point "
                           "at some time during processing. ");
    point_source.setUUID("4e42e96a-6af0-4fdd-81cb-6216ff47bf6b");
    addDefaultDimension(point_source, getName());

    Dimension packet_descriptor("WavePacketDescriptorIndex", dimension::UnsignedInteger, 1);
    packet_descriptor.setUUID("1d095eb0-099f-4800-abb6-2272be486f81");
    addDefaultDimension(packet_descriptor, getName());

    Dimension packet_offset("WaveformDataOffset", dimension::UnsignedInteger, 8);
    packet_offset.setUUID("6dee8edf-0c2a-4554-b999-20c9d5f0e7b9");
    addDefaultDimension(packet_offset, getName());

    Dimension return_point("ReturnPointWaveformLocation", dimension::UnsignedInteger, 4);
    return_point.setUUID("f0f37962-2563-4c3e-858d-28ec15a1103f");
    addDefaultDimension(return_point, getName());

    Dimension wave_x("WaveformXt", dimension::Float, 4);
    wave_x.setUUID("c0ec76eb-9121-4127-b3d7-af92ef871a2d");
    addDefaultDimension(wave_x, getName());

    Dimension wave_y("WaveformYt", dimension::Float, 4);
    wave_y.setUUID("b3f5bb56-3c25-42eb-9476-186bb6b78e3c");
    addDefaultDimension(wave_y, getName());

    Dimension wave_z("WaveformZt", dimension::Float, 4);
    wave_z.setUUID("7499ae66-462f-4d0b-a449-6e5c721fb087");
    addDefaultDimension(wave_z, getName());

    Dimension red("Red", dimension::UnsignedInteger, 2,
                  "The red image channel value associated with this point");
    red.setUUID("a42ce297-6aa2-4a62-bd29-2db19ba862d5");
    addDefaultDimension(red, getName());

    Dimension blue("Blue", dimension::UnsignedInteger, 2,
                   "The blue image channel value associated with this point");
    blue.setUUID("5c1a99c8-1829-4d5b-8735-4f6f393a7970");
    addDefaultDimension(blue, getName());

    Dimension green("Green", dimension::UnsignedInteger, 2,
                    "The green image channel value associated with this point");
    green.setUUID("7752759d-5713-48cd-9842-51db350cc979");
    addDefaultDimension(green, getName());

}

boost::property_tree::ptree Reader::toPTree() const
{
    boost::property_tree::ptree tree = pdal::Reader::toPTree();

    tree.add("Compression", this->isCompressed());
    // add more stuff here specific to this stage type

    return tree;
}



namespace iterators
{


Base::Base(pdal::drivers::las::Reader const& reader)
    : m_reader(reader)
    , m_istream(m_reader.getStreamFactory().allocate())
    , m_pointDimensions(NULL)
    , m_schema(0)
    , m_zipPoint(NULL)
    , m_unzipper(NULL)
{
    m_istream.seekg(m_reader.getPointDataOffset());

    if (m_reader.isCompressed())
    {
#ifdef PDAL_HAVE_LASZIP
        initialize();
#else
        throw pdal_error("LASzip is not enabled for this pdal::drivers::las::IteratorBase!");
#endif
    }

    return;
}


Base::~Base()
{
#ifdef PDAL_HAVE_LASZIP
    m_zipPoint.reset();
    m_unzipper.reset();
#endif

    if (m_pointDimensions)
        delete m_pointDimensions;

    m_reader.getStreamFactory().deallocate(m_istream);
}


void Base::initialize()
{
#ifdef PDAL_HAVE_LASZIP
    if (!m_zipPoint)
    {
        PointFormat format = m_reader.getPointFormat();
        boost::scoped_ptr<ZipPoint> z(new ZipPoint(format, m_reader.getVLRs()));
        m_zipPoint.swap(z);
    }

    if (!m_unzipper)
    {
        boost::scoped_ptr<LASunzipper> z(new LASunzipper());
        m_unzipper.swap(z);

        bool stat(false);
        m_istream.seekg(m_reader.getPointDataOffset(), std::ios::beg);
        stat = m_unzipper->open(m_istream, m_zipPoint->GetZipper());

        // Martin moves the stream on us
        m_zipReadStartPosition = m_istream.tellg();
        if (!stat)
        {
            std::ostringstream oss;
            const char* err = m_unzipper->get_error();
            if (err==NULL) err="(unknown error)";
            oss << "Failed to open LASzip stream: " << std::string(err);
            throw pdal_error(oss.str());
        }
    }
#endif
    return;
}

void Base::read(PointBuffer&)
{

}

void Base::setPointDimensions(PointBuffer& buffer)
{
    // Cache dimension positions
    Schema const& schema = buffer.getSchema();
    if (m_pointDimensions)
        delete m_pointDimensions;
    m_pointDimensions = new PointDimensions(schema, m_reader.getName());

}

namespace sequential
{


Reader::Reader(pdal::drivers::las::Reader const& reader, PointBuffer& buffer)
    : Base(reader)
    , pdal::ReaderSequentialIterator(reader, buffer)
{
    return;
}


Reader::~Reader()
{
    return;
}

void Reader::readBufferBeginImpl(PointBuffer& buffer)
{
    setPointDimensions(buffer);
}

boost::uint64_t Reader::skipImpl(boost::uint64_t count)
{
#ifdef PDAL_HAVE_LASZIP
    if (m_unzipper)
    {
        const boost::uint32_t pos32 = Utils::safeconvert64to32(getIndex() + count);
        m_unzipper->seek(pos32);
    }
    else
    {
        boost::uint64_t delta = Support::getPointDataSize(m_reader.getPointFormat());
        m_istream.seekg(delta * count, std::ios::cur);
    }
#else
    boost::uint64_t delta = Support::getPointDataSize(m_reader.getPointFormat());
    m_istream.seekg(delta * count, std::ios::cur);
#endif
    return count;
}


bool Reader::atEndImpl() const
{
    return getIndex() >= getStage().getNumPoints();
}


boost::uint32_t Reader::readBufferImpl(PointBuffer& data)
{
#ifdef PDAL_HAVE_LASZIP
    return m_reader.processBuffer(data,
                                  m_istream,
                                  getStage().getNumPoints()-this->getIndex(),
                                  m_unzipper.get(),
                                  m_zipPoint.get(),
                                  m_pointDimensions);
#else
    return m_reader.processBuffer(data,
                                  m_istream,
                                  getStage().getNumPoints()-this->getIndex(),
                                  NULL,
                                  NULL,
                                  m_pointDimensions);

#endif
}


} // sequential

namespace random
{

Reader::Reader(const pdal::drivers::las::Reader& reader, PointBuffer& buffer)
    : Base(reader)
    , pdal::ReaderRandomIterator(reader, buffer)
{
    return;
}


Reader::~Reader()
{
    return;
}

void Reader::readBeginImpl()
{
    // We'll assume you're not changing the schema per-read call
    setPointDimensions(getBuffer());
}

boost::uint64_t Reader::seekImpl(boost::uint64_t count)
{
#ifdef PDAL_HAVE_LASZIP
    if (m_unzipper)
    {
        const boost::uint32_t pos32 = Utils::safeconvert64to32(count);
        m_unzipper->seek(pos32);
    }
    else
    {
        boost::uint64_t delta = Support::getPointDataSize(m_reader.getPointFormat());
        m_istream.seekg(m_reader.getPointDataOffset() + delta * count);
    }
#else

    boost::uint64_t delta = Support::getPointDataSize(m_reader.getPointFormat());
    m_istream.seekg(m_reader.getPointDataOffset() + delta * count);

#endif

    return count;
}


boost::uint32_t Reader::readBufferImpl(PointBuffer& data)
{
#ifdef PDAL_HAVE_LASZIP
    return m_reader.processBuffer(data,
                                  m_istream,
                                  getStage().getNumPoints()-this->getIndex(),
                                  m_unzipper.get(),
                                  m_zipPoint.get(),
                                  m_pointDimensions);
#else
    return m_reader.processBuffer(data,
                                  m_istream,
                                  getStage().getNumPoints()-this->getIndex(),
                                  NULL,
                                  NULL,
                                  m_pointDimensions);

#endif
}


} // random
} // iterators
}
}
} // namespaces
