#ifndef NOS3_GENERIC_GNSS42DATAPROVIDER_HPP
#define NOS3_GENERIC_GNSS42DATAPROVIDER_HPP

#include <boost/property_tree/ptree.hpp>
#include <ItcLogger/Logger.hpp>
#include <generic_gnss_data_point.hpp>
#include <sim_data_42socket_provider.hpp>

namespace Nos3
{
    class Generic_gnss42DataProvider : public SimData42SocketProvider
    {
    public:
        Generic_gnss42DataProvider(const boost::property_tree::ptree& config);
        boost::shared_ptr<SimIDataPoint> get_data_point(void) const;

    private:
        ~Generic_gnss42DataProvider(void) {}
        Generic_gnss42DataProvider& operator=(const Generic_gnss42DataProvider&) { return *this; }
        int16_t _sc;
    };
}

#endif
