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

#ifndef INCLUDED_FILTERS_STATSFILTER_HPP
#define INCLUDED_FILTERS_STATSFILTER_HPP

#include <pdal/Filter.hpp>
#include <pdal/FilterIterator.hpp>

#include <pdal/Range.hpp>
#include <pdal/PointBuffer.hpp>
#include <pdal/Schema.hpp>

#include <iostream>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/moment.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/count.hpp>
#include <boost/accumulators/statistics/density.hpp>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

namespace pdal
{
namespace filters
{

namespace stats
{



typedef boost::iterator_range<std::vector<std::pair<double, double> >::iterator > histogram_type;
#ifndef PDAL_COMPILER_MSVC  // See boost ticket 6535:  https://svn.boost.org/trac/boost/ticket/6535
typedef boost::accumulators::accumulator_set<double, boost::accumulators::features< boost::accumulators::droppable<boost::accumulators::tag::density> > > density_accumulator ;
#else
typedef boost::accumulators::accumulator_set<double, boost::accumulators::features< boost::accumulators::tag::density > > density_accumulator ;
#endif
typedef boost::accumulators::accumulator_set<double, boost::accumulators::features<     boost::accumulators::droppable<boost::accumulators::tag::mean>,
        boost::accumulators::droppable<boost::accumulators::tag::max>,
        boost::accumulators::droppable<boost::accumulators::tag::min>,
        boost::accumulators::droppable<boost::accumulators::tag::count> > > summary_accumulator;
class PDAL_DLL Summary
{
public:

    double minimum() const
    {
        return boost::accumulators::min(m_summary);
    }
    double maximum() const
    {
        return boost::accumulators::max(m_summary);
    }
    double average() const
    {
        return boost::accumulators::mean(m_summary);
    }
    boost::uint64_t count() const
    {
        return boost::accumulators::count(m_summary);
    }
    histogram_type histogram() const
    {
        return boost::accumulators::density(m_histogram);
    }

    boost::property_tree::ptree toPTree() const;

private:
    summary_accumulator m_summary;
    density_accumulator m_histogram;
    std::vector<double> m_sample;
    boost::uint32_t m_sample_size;
    boost::random::mt19937 m_rng;
    boost::random::uniform_int_distribution<> m_distribution;
public:

    Summary(boost::uint32_t num_bins=20,
            boost::uint32_t sample_size=1000,
            boost::uint32_t cache_size=1000,
            boost::uint32_t seed=0)
        : m_histogram(boost::accumulators::tag::density::num_bins = num_bins,
                      boost::accumulators::tag::density::cache_size = cache_size)
        , m_sample_size(sample_size)
        , m_distribution(0, cache_size)
    {
        if (seed != 0)
        {
            m_rng.seed(seed);
            m_distribution.reset();
        }

        return;
    }

    void reset()
    {
        m_summary.drop<boost::accumulators::tag::mean>();
        m_summary.drop<boost::accumulators::tag::count>();
        m_summary.drop<boost::accumulators::tag::max>();
        m_summary.drop<boost::accumulators::tag::min>();
        m_histogram.drop<boost::accumulators::tag::density>();
        return;
    }

    template<class T> inline void insert(T value)
    {
        m_summary(static_cast<double>(value));
        m_histogram(static_cast<double>(value));

        int sample = m_distribution(m_rng);
        if (static_cast<boost::uint32_t>(sample) < m_sample_size)
            m_sample.push_back(static_cast<double>(value));
        return;
    }

};

typedef boost::shared_ptr<Summary> SummaryPtr;

} // namespace stats

// this is just a pass-thorugh filter, which collects some stats about the points
// that are fed through it
class PDAL_DLL Stats : public Filter
{
public:
    SET_STAGE_NAME("filters.stats", "Statistics Filter")

    Stats(Stage& prevStage, const Options&);
    Stats(Stage& prevStage);
    ~Stats();

    virtual void initialize();
    virtual const Options getDefaultOptions() const;

    bool supportsIterator(StageIteratorType t) const
    {
        if (t == StageIterator_Sequential) return true;
        if (t == StageIterator_Random) return true;
        if (t == StageIterator_Block) return true;
        return false;
    }

    pdal::StageSequentialIterator* createSequentialIterator(PointBuffer& buffer) const;
    pdal::StageRandomIterator* createRandomIterator(PointBuffer&) const
    {
        return 0;    // BUG: add this
    }

    void processBuffer(PointBuffer& data) const;



    // return a tree like this:
    //    X:
    //        cout: 100
    //        min: 1.0
    //        max: 100.0
    //    Y:
    //        cout: 100
    //        min: 11.0
    //        max: 110.0
    //
    // boost::property_tree::ptree toStatsPTree() const;

private:

    Stats& operator=(const Stats&); // not implemented
    Stats(const Stats&); // not implemented
};



namespace iterators
{
namespace sequential
{

typedef boost::shared_ptr<Dimension> DimensionPtr;

class PDAL_DLL Stats : public pdal::FilterSequentialIterator
{
public:
    Stats(const pdal::filters::Stats& filter, PointBuffer& buffer);
    boost::property_tree::ptree toPTree() const;
    stats::Summary const& getStats(Dimension const& dim) const;
    void reset();

protected:
    virtual void readBufferBeginImpl(PointBuffer&);

private:
    boost::uint64_t skipImpl(boost::uint64_t);
    boost::uint32_t readBufferImpl(PointBuffer&);
    bool atEndImpl() const;

    const pdal::filters::Stats& m_statsFilter;

    std::vector<DimensionPtr> m_dimensions;

    double getValue(PointBuffer& data, Dimension& dim, boost::uint32_t pointIndex);

    std::multimap<DimensionPtr,stats::SummaryPtr> m_stats; // one Stats item per field in the schema
};


}
} // iterators::sequential

}
} // namespaces

#endif
