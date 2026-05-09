#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>

#include <ItcLogger/Logger.hpp>

#include <generic_tt_c_hardware_model.hpp>

namespace Nos3
{
    REGISTER_HARDWARE_MODEL(Generic_tt_cHardwareModel, "GENERIC_TT_C");

    extern ItcLogger::Logger *sim_logger;

    namespace
    {
        constexpr double kEarthRadiusM    = 6378137.0;       // WGS-84 equatorial radius
        constexpr double kSpeedOfLightMps = 299792458.0;     // c (exact)
        constexpr double kPi              = 3.14159265358979323846;
        constexpr double kDeg             = 180.0 / kPi;

        // Ground station table mirrors nos3/cfg/InOut/Inp_Sim.txt:53-61.
        // Lat/lon in degrees, altitude in metres above ellipsoid.
        struct GroundStation { const char* name; double lat_deg; double lon_deg; double alt_m; };
        const GroundStation kGroundStations[] = {
            { "Svalbard",    78.23,  15.40,    0.0 },  // Canonical EO1 GS (SvalSat)
            { "DTU",         55.79,  12.52,    0.0 },  // DTU Lyngby campus
            { "GSFC",        37.0,  -77.0,     0.0 },
            { "South_Point", 19.0,  -155.6,    0.0 },
            { "Dongara",    -29.0,   115.4,    0.0 },
            { "Santiago",   -33.0,   -71.0,    0.0 },
        };
        constexpr size_t kNumStations = sizeof(kGroundStations) / sizeof(kGroundStations[0]);

        // Convert geodetic lat/lon/alt to ECEF (spherical-Earth approximation;
        // accurate enough for visibility/pass-geometry use).
        void lla_to_ecef(double lat_deg, double lon_deg, double alt_m, double out[3])
        {
            const double lat = lat_deg / kDeg;
            const double lon = lon_deg / kDeg;
            const double r   = kEarthRadiusM + alt_m;
            out[0] = r * std::cos(lat) * std::cos(lon);
            out[1] = r * std::cos(lat) * std::sin(lon);
            out[2] = r * std::sin(lat);
        }

        // ECEF look-vector (sat - gs) -> ENU components at the ground station.
        void ecef_diff_to_enu(double dx, double dy, double dz,
                              double gs_lat_deg, double gs_lon_deg,
                              double& east, double& north, double& up)
        {
            const double lat = gs_lat_deg / kDeg;
            const double lon = gs_lon_deg / kDeg;
            const double sl  = std::sin(lat), cl = std::cos(lat);
            const double sL  = std::sin(lon), cL = std::cos(lon);
            east  = -sL * dx +  cL * dy;
            north = -sl * cL * dx - sl * sL * dy + cl * dz;
            up    =  cl * cL * dx + cl * sL * dy + sl * dz;
        }
    }

    Generic_tt_cHardwareModel::Generic_tt_cHardwareModel(const boost::property_tree::ptree& config)
        : SimIHardwareModel(config),
          _enabled(1), _tick_count(0),
          _in_pass(false), _pass_number(0), _pass_count(0),
          _last_slant_range_m(0.0), _have_last_range(false),
          _packets_downlinked(0), _bytes_downlinked(0), _packets_dropped(0)
    {
        std::string connection_string = config.get("common.nos-connection-string", "tcp://127.0.0.1:12001");
        sim_logger->info("Generic_tt_cHardwareModel:  NOS Engine connection string: %s.", connection_string.c_str());

        std::string dp_name = config.get("simulator.hardware-model.data-provider.type", "GENERIC_TT_C_42_PROVIDER");
        _generic_tt_c_dp = SimDataProviderFactory::Instance().Create(dp_name, config);
        sim_logger->info("Generic_tt_cHardwareModel:  Data provider %s created.", dp_name.c_str());

        _elev_mask_deg      = config.get("simulator.hardware-model.elevation-mask-deg", 5.0);
        _carrier_hz         = config.get("simulator.hardware-model.carrier-hz", 2200000000.0); // S-band default
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
        _time_bus->add_time_tick_callback(std::bind(&Generic_tt_cHardwareModel::tick_callback,
                                                    this, std::placeholders::_1));
        sim_logger->info("Generic_tt_cHardwareModel:  Now on time bus named %s.", time_bus_name.c_str());

        sim_logger->info("Generic_tt_cHardwareModel:  Construction complete.");
    }

    Generic_tt_cHardwareModel::~Generic_tt_cHardwareModel(void)
    {
        _time_bus.reset();
        delete _generic_tt_c_dp;
        _generic_tt_c_dp = nullptr;
    }

    void Generic_tt_cHardwareModel::command_callback(NosEngine::Common::Message msg)
    {
        NosEngine::Common::DataBufferOverlay dbf(const_cast<NosEngine::Utility::Buffer&>(msg.buffer));
        sim_logger->info("Generic_tt_cHardwareModel::command_callback:  Received: %s.", dbf.data);

        std::string command(dbf.data);
        boost::to_upper(command);
        std::string response = "Generic_tt_c::command_callback:  INVALID COMMAND (try HELP, ENABLE, DISABLE, STOP)";
        if (command == "HELP")
        {
            response = "Generic_tt_c: HELP, ENABLE, DISABLE, STOP";
        }
        else if (command == "ENABLE")
        {
            _enabled = 1;
            response = "Generic_tt_c: Enabled";
        }
        else if (command == "DISABLE")
        {
            _enabled = 0;
            response = "Generic_tt_c: Disabled";
        }
        else if (command == "STOP")
        {
            _keep_running = false;
            response = "Generic_tt_c: Stopping";
        }
        _command_node->send_reply_message_async(msg, response.size(), response.c_str());
    }

    void Generic_tt_cHardwareModel::tick_callback(NosEngine::Common::SimTime /*t*/)
    {
        _tick_count++;
        if (!_enabled) return;
        if ((_tick_count % _ticks_per_emission) != 0) return;

        boost::shared_ptr<Generic_tt_cDataPoint> dp =
            boost::dynamic_pointer_cast<Generic_tt_cDataPoint>(_generic_tt_c_dp->get_data_point());
        const bool dp_valid = (dp && dp->is_valid());
        if (!dp_valid)
        {
            sim_logger->debug("Generic_tt_cHardwareModel::tick_callback:  data point not yet valid; emitting placeholder");
        }

        const double sat_ecef[3] = {
            dp_valid ? dp->get_pos_w_x() : 0.0,
            dp_valid ? dp->get_pos_w_y() : 0.0,
            dp_valid ? dp->get_pos_w_z() : 0.0
        };
        const double sat_vel[3]  = {
            dp_valid ? dp->get_vel_w_x() : 0.0,
            dp_valid ? dp->get_vel_w_y() : 0.0,
            dp_valid ? dp->get_vel_w_z() : 0.0
        };

        // Pick the highest-elevation ground station among those above the mask.
        const GroundStation* best_gs    = nullptr;
        double best_elev_deg            = -90.0;
        double best_slant_m             = 0.0;
        double best_range_rate_mps      = 0.0;

        for (size_t i = 0; i < kNumStations; ++i)
        {
            const GroundStation& gs = kGroundStations[i];
            double gs_ecef[3];
            lla_to_ecef(gs.lat_deg, gs.lon_deg, gs.alt_m, gs_ecef);

            const double dx = sat_ecef[0] - gs_ecef[0];
            const double dy = sat_ecef[1] - gs_ecef[1];
            const double dz = sat_ecef[2] - gs_ecef[2];
            double east, north, up;
            ecef_diff_to_enu(dx, dy, dz, gs.lat_deg, gs.lon_deg, east, north, up);

            const double slant_m = std::sqrt(east * east + north * north + up * up);
            if (slant_m <= 0.0) continue;

            const double elev_deg = kDeg * std::asin(up / slant_m);

            // Range-rate via projection of sat velocity onto sat<-gs ECEF unit vector.
            const double rate_mps = (dx * sat_vel[0] + dy * sat_vel[1] + dz * sat_vel[2]) / slant_m;

            if (elev_deg > best_elev_deg)
            {
                best_elev_deg       = elev_deg;
                best_slant_m        = slant_m;
                best_range_rate_mps = rate_mps;
                best_gs             = &gs;
            }
        }

        const bool acquired      = (best_gs != nullptr) && (best_elev_deg >= _elev_mask_deg);
        const char* link_state   = acquired ? "ACQUIRED" : "IDLE";
        const char* gs_label     = (best_gs != nullptr) ? best_gs->name : "NONE";
        const double slant_km    = best_slant_m / 1000.0;
        // Doppler: f_d = -v_r * f_c / c, sign chosen so approach -> positive.
        const double doppler_hz  = -best_range_rate_mps * _carrier_hz / kSpeedOfLightMps;

        if (acquired && !_in_pass)
        {
            _in_pass = true;
            _pass_number++;
            _pass_count = _pass_number;
        }
        else if (!acquired && _in_pass)
        {
            _in_pass = false;
        }

        // Throughput accumulation while in pass. Placeholder rates: 128 packets and
        // 16 KiB per emission tick, with a 3-packet drop at low elevation.
        if (acquired)
        {
            _packets_downlinked += 128;
            _bytes_downlinked   += 128 * 128;
            if (best_elev_deg < (_elev_mask_deg + 5.0))
            {
                _packets_dropped += 3;
            }
        }

        _last_slant_range_m = best_slant_m;
        _have_last_range    = true;

        sim_logger->info("[TTC] ground_station=%s link_state=%s elevation_deg=%.3f "
                         "slant_range_km=%.3f doppler_hz=%.3f pass_number=%u pass_count=%u "
                         "packets_downlinked=%u bytes_downlinked=%u packets_dropped=%u",
                         gs_label, link_state, best_elev_deg, slant_km, doppler_hz,
                         _pass_number, _pass_count,
                         _packets_downlinked, _bytes_downlinked, _packets_dropped);
    }
}
