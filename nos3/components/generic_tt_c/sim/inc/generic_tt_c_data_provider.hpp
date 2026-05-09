#ifndef NOS3_GENERIC_TT_CDATAPROVIDER_HPP
#define NOS3_GENERIC_TT_CDATAPROVIDER_HPP

#include <boost/property_tree/xml_parser.hpp>
#include <ItcLogger/Logger.hpp>
#include <generic_tt_c_data_point.hpp>
#include <sim_i_data_provider.hpp>

namespace Nos3
{
    class Generic_tt_cDataProvider : public SimIDataProvider
    {
    public:
        Generic_tt_cDataProvider(const boost::property_tree::ptree& config);
        boost::shared_ptr<SimIDataPoint> get_data_point(void) const;

    private:
        ~Generic_tt_cDataProvider(void) {}
        Generic_tt_cDataProvider& operator=(const Generic_tt_cDataProvider&) { return *this; }
    };
}

#endif
