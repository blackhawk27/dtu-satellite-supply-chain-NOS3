#ifndef NOS3_GENERIC_GNSSDATAPROVIDER_HPP
#define NOS3_GENERIC_GNSSDATAPROVIDER_HPP

#include <boost/property_tree/xml_parser.hpp>
#include <ItcLogger/Logger.hpp>
#include <generic_gnss_data_point.hpp>
#include <sim_i_data_provider.hpp>

namespace Nos3
{
    class Generic_gnssDataProvider : public SimIDataProvider
    {
    public:
        Generic_gnssDataProvider(const boost::property_tree::ptree& config);
        boost::shared_ptr<SimIDataPoint> get_data_point(void) const;

    private:
        ~Generic_gnssDataProvider(void) {}
        Generic_gnssDataProvider& operator=(const Generic_gnssDataProvider&) { return *this; }
    };
}

#endif
