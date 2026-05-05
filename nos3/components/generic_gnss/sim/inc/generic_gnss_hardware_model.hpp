#ifndef NOS3_GENERIC_GNSSHARDWAREMODEL_HPP
#define NOS3_GENERIC_GNSSHARDWAREMODEL_HPP

#include <cstdint>
#include <memory>
#include <string>

#include <boost/property_tree/ptree.hpp>

#include <Client/Bus.hpp>
#include <Uart/Client/Uart.hpp>

#include <sim_i_data_provider.hpp>
#include <generic_gnss_data_point.hpp>
#include <sim_i_hardware_model.hpp>

namespace Nos3
{
    /*
     * Generic GNSS hardware model. Subscribes to 42 spacecraft state and
     * synthesizes multi-constellation visibility metrics (NumSats by
     * constellation, PDOP/HDOP/VDOP, solution type) on every tick. Emits a
     * single tagged "[GNSS] ..." log line that Logstash parses into the
     * subsystem:GNSS index. Distinct from the GPS app (NMEA / position-fix-
     * only) so users can filter the two cleanly in Kibana.
     *
     * Each tick also (a) writes the same lat/lon/alt to a UART node so the
     * generic_gnss FSW app reads the position over a NOS Engine bus the way
     * a real receiver would, and (b) emits a "[GNSS_TRUTH]" log line. Kibana
     * joins the truth log against the FSW HK echo so spoofing a single hop
     * (the UART) shows up as a divergence between the two series.
     */
    class Generic_gnssHardwareModel : public SimIHardwareModel
    {
    public:
        Generic_gnssHardwareModel(const boost::property_tree::ptree& config);
        ~Generic_gnssHardwareModel(void);

    private:
        void command_callback(NosEngine::Common::Message msg);
        void tick_callback(NosEngine::Common::SimTime t);

        std::unique_ptr<NosEngine::Client::Bus> _time_bus;
        std::unique_ptr<NosEngine::Uart::Uart>  _uart_connection;
        SimIDataProvider* _generic_gnss_dp;

        std::uint8_t  _enabled;
        std::uint32_t _tick_count;

        std::uint32_t _ticks_per_emission;
    };
}

#endif
