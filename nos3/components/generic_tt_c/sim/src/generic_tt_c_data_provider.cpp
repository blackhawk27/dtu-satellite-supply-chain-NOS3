#include <generic_tt_c_data_provider.hpp>
#include <boost/make_shared.hpp>

namespace Nos3
{
    REGISTER_DATA_PROVIDER(Generic_tt_cDataProvider, "GENERIC_TT_C_PROVIDER");

    extern ItcLogger::Logger *sim_logger;

    Generic_tt_cDataProvider::Generic_tt_cDataProvider(const boost::property_tree::ptree& config)
        : SimIDataProvider(config)
    {
        sim_logger->trace("Generic_tt_cDataProvider::Generic_tt_cDataProvider:  Constructor executed");
    }

    boost::shared_ptr<SimIDataPoint> Generic_tt_cDataProvider::get_data_point(void) const
    {
        // Stub provider: returns an empty (invalid) data point. The active configuration
        // uses GENERIC_TT_C_42_PROVIDER, which subscribes to 42 state.
        boost::shared_ptr<Sim42DataPoint> dp42 = boost::make_shared<Sim42DataPoint>();
        return boost::shared_ptr<SimIDataPoint>(new Generic_tt_cDataPoint(0, dp42));
    }
}
