#ifndef NOS3_GENERIC_GNSSDATAPOINT_HPP
#define NOS3_GENERIC_GNSSDATAPOINT_HPP

#include <boost/shared_ptr.hpp>
#include <sim_42data_point.hpp>

namespace Nos3
{
    /*
     * Holds per-tick spacecraft state from 42 (ECEF position + altitude)
     * for the GNSS constellation-health computation. The hardware model
     * synthesizes visibility numbers from altitude / latitude proxies; this
     * data point is just a thin wrapper around the 42 frame.
     */
    class Generic_gnssDataPoint : public SimIDataPoint
    {
    public:
        Generic_gnssDataPoint(int16_t spacecraft, const boost::shared_ptr<Sim42DataPoint> dp);
        ~Generic_gnssDataPoint(void) {}

        std::string to_string(void) const;

        bool   is_valid(void) const { parse_data_point(); return _valid; }
        double get_pos_w_x(void) const { parse_data_point(); return _posW[0]; }
        double get_pos_w_y(void) const { parse_data_point(); return _posW[1]; }
        double get_pos_w_z(void) const { parse_data_point(); return _posW[2]; }

    private:
        Generic_gnssDataPoint(void) {}
        Generic_gnssDataPoint(const Generic_gnssDataPoint&) {}

        inline void parse_data_point(void) const { if (_not_parsed) do_parsing(); }
        void do_parsing(void) const;

        mutable Sim42DataPoint _dp;
        int16_t _sc;
        mutable bool _not_parsed;
        mutable bool _valid;
        mutable double _posW[3];
    };
}

#endif
