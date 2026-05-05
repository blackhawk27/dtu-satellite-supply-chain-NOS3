#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>

#include <ItcLogger/Logger.hpp>

#include <generic_gnss_hardware_model.hpp>
#include <sim_coordinate_transformations.hpp>

namespace Nos3
{
    REGISTER_HARDWARE_MODEL(Generic_gnssHardwareModel, "GENERIC_GNSS");

    extern ItcLogger::Logger *sim_logger;

    namespace
    {
        constexpr double kEarthRadiusM = 6378137.0;
    }

    Generic_gnssHardwareModel::Generic_gnssHardwareModel(const boost::property_tree::ptree& config)
        : SimIHardwareModel(config),
          _enabled(1), _tick_count(0)
    {
        std::string connection_string = config.get("common.nos-connection-string", "tcp://127.0.0.1:12001");
        sim_logger->info("Generic_gnssHardwareModel:  NOS Engine connection string: %s.", connection_string.c_str());

        std::string dp_name = config.get("simulator.hardware-model.data-provider.type", "GENERIC_GNSS_42_PROVIDER");
        _generic_gnss_dp = SimDataProviderFactory::Instance().Create(dp_name, config);
        sim_logger->info("Generic_gnssHardwareModel:  Data provider %s created.", dp_name.c_str());

        _ticks_per_emission = config.get("simulator.hardware-model.ticks-per-emission", 100);

        std::string time_bus_name = "command";
        if (config.get_child_optional("hardware-model.connections"))
        {
            BOOST_FOREACH(const boost::property_tree::ptree::value_type& v,
                          config.get_child("hardware-model.connections"))
            {
                if (v.second.get("type", "").compare("time") == 0)
                {
                    time_bus_name = v.second.get("bus-name", "command");
                    break;
                }
            }
        }
        _time_bus.reset(new NosEngine::Client::Bus(_hub, connection_string, time_bus_name));
        _time_bus->add_time_tick_callback(std::bind(&Generic_gnssHardwareModel::tick_callback,
                                                    this, std::placeholders::_1));
        sim_logger->info("Generic_gnssHardwareModel:  Now on time bus named %s.", time_bus_name.c_str());

        /* Open the UART node so the FSW (which connects to the same bus on the
         * same node-port) sees a position line every tick. Default to usart_29
         * to avoid clashing with the heritage GPS UART (usart_1). */
        std::string uart_bus_name = "usart_29";
        int         uart_node_port = 29;
        if (config.get_child_optional("simulator.hardware-model.connections"))
        {
            BOOST_FOREACH(const boost::property_tree::ptree::value_type& v,
                          config.get_child("simulator.hardware-model.connections"))
            {
                if (v.second.get("type", "").compare("usart") == 0)
                {
                    uart_bus_name  = v.second.get("bus-name", uart_bus_name);
                    uart_node_port = v.second.get("node-port", uart_node_port);
                    break;
                }
            }
        }
        _uart_connection.reset(new NosEngine::Uart::Uart(_hub, config.get("simulator.name", "generic-gnss-sim"),
                                                        connection_string, uart_bus_name));
        _uart_connection->open(uart_node_port);
        sim_logger->info("Generic_gnssHardwareModel:  UART %s node %d open.",
                         uart_bus_name.c_str(), uart_node_port);

        sim_logger->info("Generic_gnssHardwareModel:  Construction complete.");
    }

    Generic_gnssHardwareModel::~Generic_gnssHardwareModel(void)
    {
        if (_uart_connection)
        {
            _uart_connection->close();
            _uart_connection.reset();
        }
        _time_bus.reset();
        delete _generic_gnss_dp;
        _generic_gnss_dp = nullptr;
    }

    void Generic_gnssHardwareModel::command_callback(NosEngine::Common::Message msg)
    {
        NosEngine::Common::DataBufferOverlay dbf(const_cast<NosEngine::Utility::Buffer&>(msg.buffer));
        sim_logger->info("Generic_gnssHardwareModel::command_callback:  Received: %s.", dbf.data);

        std::string command(dbf.data);
        boost::to_upper(command);
        std::string response = "Generic_gnss::command_callback:  INVALID COMMAND (try HELP, ENABLE, DISABLE, STOP)";
        if (command == "HELP")
        {
            response = "Generic_gnss: HELP, ENABLE, DISABLE, STOP";
        }
        else if (command == "ENABLE")
        {
            _enabled = 1;
            response = "Generic_gnss: Enabled";
        }
        else if (command == "DISABLE")
        {
            _enabled = 0;
            response = "Generic_gnss: Disabled";
        }
        else if (command == "STOP")
        {
            _keep_running = false;
            response = "Generic_gnss: Stopping";
        }
        _command_node->send_reply_message_async(msg, response.size(), response.c_str());
    }

    void Generic_gnssHardwareModel::tick_callback(NosEngine::Common::SimTime /*t*/)
    {
        _tick_count++;
        if (!_enabled) return;
        if ((_tick_count % _ticks_per_emission) != 0) return;

        boost::shared_ptr<Generic_gnssDataPoint> dp =
            boost::dynamic_pointer_cast<Generic_gnssDataPoint>(_generic_gnss_dp->get_data_point());
        const bool dp_valid = (dp && dp->is_valid());
        if (!dp_valid)
        {
            sim_logger->debug("Generic_gnssHardwareModel::tick_callback:  data point not yet valid; emitting placeholder");
        }

        const double x = dp_valid ? dp->get_pos_w_x() : 0.0;
        const double y = dp_valid ? dp->get_pos_w_y() : 0.0;
        const double z = dp_valid ? dp->get_pos_w_z() : 0.0;
        const double r = std::sqrt(x*x + y*y + z*z);
        const double alt_km = std::max(0.0, (r - kEarthRadiusM) / 1000.0);

        /* Convert ECEF to WGS84 geodetic. ECEF2LLA already returns degrees
         * (the function converts radians->degrees internally for both lambda
         * and phi_gd) and altitude in meters. ECEF2LLA tolerates a zero
         * vector (returns lat=lon=0, alt=-Re), so the placeholder branch
         * upstream is fine. */
        double lat_deg = 0.0, lon_deg = 0.0, alt_m = 0.0;
        SimCoordinateTransformations::ECEF2LLA(x, y, z, lat_deg, lon_deg, alt_m);

        // Coarse visibility model: at LEO (~400 km altitude) NovAtel typically
        // sees 8-12 GPS satellites. Modulate by tick count for plausible variability.
        const int n_gps = (alt_km > 100.0) ? (8 + (static_cast<int>(_tick_count / _ticks_per_emission) % 5)) : 0;
        // Multi-constellation is currently stubbed in the OEM615 sim
        // (gps_sim_hardware_model_OEM615.cpp:793-795 TODO). Leave at zero so the
        // GNSS dashboard reflects the present capability honestly.
        const int n_glonass = 0;
        const int n_galileo = 0;
        const int n_beidou  = 0;
        const int n_total   = n_gps + n_glonass + n_galileo + n_beidou;

        const char* soln_type = (n_total >= 4) ? "FIX_3D"
                              : (n_total >= 2) ? "FLOAT"
                              : "NONE";

        const float pdop = (n_total >= 8) ? 1.8f
                         : (n_total >= 6) ? 2.4f
                         : (n_total >= 4) ? 3.5f
                         : 99.9f;
        const float hdop = pdop * 0.7f;
        const float vdop = pdop * 0.7f;

        sim_logger->info("[GNSS] solution_type=%s num_sats_total=%d num_sats_gps=%d "
                         "num_sats_glonass=%d num_sats_galileo=%d num_sats_beidou=%d "
                         "pdop=%.2f hdop=%.2f vdop=%.2f",
                         soln_type, n_total, n_gps, n_glonass, n_galileo, n_beidou,
                         pdop, hdop, vdop);

        /* Truth log: position before the bus, the value an attacker cannot
         * tamper with from outside the simulator. Logstash extracts these as
         * gnss_truth_lat / gnss_truth_lon / gnss_truth_alt_km so Kibana can
         * compute the delta against the FSW HK echo (gnss_lat / gnss_lon). */
        sim_logger->info("[GNSS_TRUTH] lat=%.6f lon=%.6f alt_m=%.2f",
                         lat_deg, lon_deg, alt_m);

        /* Bus emission: this is the value the FSW reads. A spoof attack
         * would replace these bytes between sim->FSW; the truth log above is
         * unchanged so the divergence is observable in Kibana. */
        if (_uart_connection)
        {
            char line[128];
            const int n = std::snprintf(line, sizeof(line), "GNSS,%.6f,%.6f,%.2f\n",
                                        lat_deg, lon_deg, alt_m);
            if (n > 0 && static_cast<size_t>(n) < sizeof(line))
            {
                _uart_connection->write(reinterpret_cast<const uint8_t*>(line),
                                        static_cast<size_t>(n));
            }
        }
    }
}
