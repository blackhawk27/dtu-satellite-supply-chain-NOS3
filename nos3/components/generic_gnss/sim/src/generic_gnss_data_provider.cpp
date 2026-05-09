#include <generic_gnss_data_provider.hpp>
#include <boost/make_shared.hpp>

namespace Nos3
{
    REGISTER_DATA_PROVIDER(Generic_gnssDataProvider, "GENERIC_GNSS_PROVIDER");

    extern ItcLogger::Logger *sim_logger;

    Generic_gnssDataProvider::Generic_gnssDataProvider(const boost::property_tree::ptree& config)
        : SimIDataProvider(config)
    {
        sim_logger->trace("Generic_gnssDataProvider::Generic_gnssDataProvider:  Constructor executed");
    }

    boost::shared_ptr<SimIDataPoint> Generic_gnssDataProvider::get_data_point(void) const
    {
        boost::shared_ptr<Sim42DataPoint> dp42 = boost::make_shared<Sim42DataPoint>();
        return boost::shared_ptr<SimIDataPoint>(new Generic_gnssDataPoint(0, dp42));
    }
}
