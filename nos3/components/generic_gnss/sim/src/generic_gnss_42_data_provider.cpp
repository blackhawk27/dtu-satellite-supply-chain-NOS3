#include <generic_gnss_42_data_provider.hpp>

namespace Nos3
{
    REGISTER_DATA_PROVIDER(Generic_gnss42DataProvider, "GENERIC_GNSS_42_PROVIDER");

    extern ItcLogger::Logger *sim_logger;

    Generic_gnss42DataProvider::Generic_gnss42DataProvider(const boost::property_tree::ptree& config)
        : SimData42SocketProvider(config)
    {
        sim_logger->trace("Generic_gnss42DataProvider::Generic_gnss42DataProvider:  Constructor executed");
        connect_reader_thread_as_42_socket_client(
            config.get("simulator.hardware-model.data-provider.hostname", "localhost"),
            config.get("simulator.hardware-model.data-provider.port", 4242));
        _sc = config.get("simulator.hardware-model.data-provider.spacecraft", 0);
    }

    boost::shared_ptr<SimIDataPoint> Generic_gnss42DataProvider::get_data_point(void) const
    {
        const boost::shared_ptr<Sim42DataPoint> dp42 =
            boost::dynamic_pointer_cast<Sim42DataPoint>(SimData42SocketProvider::get_data_point());
        SimIDataPoint *dp = new Generic_gnssDataPoint(_sc, dp42);
        return boost::shared_ptr<SimIDataPoint>(dp);
    }
}
