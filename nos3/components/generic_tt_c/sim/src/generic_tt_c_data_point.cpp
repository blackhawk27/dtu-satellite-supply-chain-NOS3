#include <ItcLogger/Logger.hpp>
#include <sstream>
#include <iomanip>
#include <generic_tt_c_data_point.hpp>

namespace Nos3
{
    extern ItcLogger::Logger *sim_logger;

    Generic_tt_cDataPoint::Generic_tt_cDataPoint(int16_t spacecraft, const boost::shared_ptr<Sim42DataPoint> dp)
        : _dp(*dp), _sc(spacecraft), _not_parsed(true), _valid(false)
    {
        _posW[0] = _posW[1] = _posW[2] = 0.0;
        _velW[0] = _velW[1] = _velW[2] = 0.0;
        sim_logger->trace("Generic_tt_cDataPoint::Generic_tt_cDataPoint:  42 Constructor executed");
    }

    void Generic_tt_cDataPoint::do_parsing(void) const
    {
        try {
            std::string base;
            base.append("SC[").append(std::to_string(_sc)).append("].");

            std::string keyP = base + "GPS[0].PosW";
            std::string keyV = base + "GPS[0].VelW";

            std::string sP = _dp.get_value_for_key(keyP);
            std::string sV = _dp.get_value_for_key(keyV);

            if (sP.empty() || sV.empty()) {
                sim_logger->debug("Generic_tt_cDataPoint::do_parsing:  empty SC[%d].GPS[0].Pos/VelW frame, deferring", _sc);
                return;
            }

            for (size_t i = 0; i < sP.size(); ++i) if (sP[i] == ',' || sP[i] == '[' || sP[i] == ']') sP[i] = ' ';
            for (size_t i = 0; i < sV.size(); ++i) if (sV[i] == ',' || sV[i] == '[' || sV[i] == ']') sV[i] = ' ';

            std::stringstream ssP(sP);
            std::stringstream ssV(sV);
            ssP >> _posW[0] >> _posW[1] >> _posW[2];
            ssV >> _velW[0] >> _velW[1] >> _velW[2];

            _valid = !ssP.fail() && !ssV.fail();
            _not_parsed = false;
        }
        catch (const std::exception& e) {
            sim_logger->error("Generic_tt_cDataPoint::do_parsing:  Parsing exception %s", e.what());
        }
    }

    std::string Generic_tt_cDataPoint::to_string(void) const
    {
        parse_data_point();
        std::stringstream ss;
        ss << std::fixed << std::setprecision(3);
        ss << "Generic_tt_c Data Point: " << (_valid ? "Valid" : "INVALID")
           << " PosW(m): " << _posW[0] << "," << _posW[1] << "," << _posW[2]
           << " VelW(m/s): " << _velW[0] << "," << _velW[1] << "," << _velW[2];
        return ss.str();
    }
}
