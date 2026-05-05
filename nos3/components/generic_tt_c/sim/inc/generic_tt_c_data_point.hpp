#ifndef NOS3_GENERIC_TT_CDATAPOINT_HPP
#define NOS3_GENERIC_TT_CDATAPOINT_HPP

#include <boost/shared_ptr.hpp>
#include <sim_42data_point.hpp>

namespace Nos3
{
    /*
     * Holds per-tick spacecraft state (ECEF position + velocity from 42)
     * for the TT&C link-budget computation. The hardware model walks the
     * configured ground-station table each tick and produces the link
     * fields; this data point is just a thin wrapper around the 42 frame.
     */
    class Generic_tt_cDataPoint : public SimIDataPoint
    {
    public:
        Generic_tt_cDataPoint(int16_t spacecraft, const boost::shared_ptr<Sim42DataPoint> dp);
        ~Generic_tt_cDataPoint(void) {}

        std::string to_string(void) const;

        bool   is_valid(void)  const { parse_data_point(); return _valid; }
        double get_pos_w_x(void) const { parse_data_point(); return _posW[0]; }
        double get_pos_w_y(void) const { parse_data_point(); return _posW[1]; }
        double get_pos_w_z(void) const { parse_data_point(); return _posW[2]; }
        double get_vel_w_x(void) const { parse_data_point(); return _velW[0]; }
        double get_vel_w_y(void) const { parse_data_point(); return _velW[1]; }
        double get_vel_w_z(void) const { parse_data_point(); return _velW[2]; }

    private:
        Generic_tt_cDataPoint(void) {}
        Generic_tt_cDataPoint(const Generic_tt_cDataPoint&) {}

        inline void parse_data_point(void) const { if (_not_parsed) do_parsing(); }
        void do_parsing(void) const;

        mutable Sim42DataPoint _dp;
        int16_t _sc;
        mutable bool _not_parsed;
        mutable bool _valid;
        mutable double _posW[3];
        mutable double _velW[3];
    };
}

#endif
