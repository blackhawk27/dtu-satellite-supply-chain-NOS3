#include <ItcLogger/Logger.hpp>
#include <sstream>
#include <iomanip>
#include <generic_gnss_data_point.hpp>

namespace Nos3
{
    extern ItcLogger::Logger *sim_logger;

    Generic_gnssDataPoint::Generic_gnssDataPoint(int16_t spacecraft, const boost::shared_ptr<Sim42DataPoint> dp)
        : _dp(*dp), _sc(spacecraft), _not_parsed(true), _valid(false)
    {
        _posW[0] = _posW[1] = _posW[2] = 0.0;
        sim_logger->trace("Generic_gnssDataPoint::Generic_gnssDataPoint:  42 Constructor executed");
    }

    void Generic_gnssDataPoint::do_parsing(void) const
    {
        try {
            std::string base;
            base.append("SC[").append(std::to_string(_sc)).append("].");
            std::string keyP = base + "GPS[0].PosW";
            std::string sP = _dp.get_value_for_key(keyP);
            if (sP.empty()) {
                sim_logger->debug("Generic_gnssDataPoint::do_parsing:  empty SC[%d].GPS[0].PosW frame, deferring", _sc);
                return;
            }
            for (size_t i = 0; i < sP.size(); ++i) if (sP[i] == ',' || sP[i] == '[' || sP[i] == ']') sP[i] = ' ';
            std::stringstream ssP(sP);
            ssP >> _posW[0] >> _posW[1] >> _posW[2];
            _valid = !ssP.fail();
            _not_parsed = false;
        }
        catch (const std::exception& e) {
            sim_logger->error("Generic_gnssDataPoint::do_parsing:  Parsing exception %s", e.what());
        }
    }

    std::string Generic_gnssDataPoint::to_string(void) const
    {
        parse_data_point();
        std::stringstream ss;
        ss << std::fixed << std::setprecision(3);
        ss << "Generic_gnss Data Point: " << (_valid ? "Valid" : "INVALID")
           << " PosW(m): " << _posW[0] << "," << _posW[1] << "," << _posW[2];
        return ss.str();
    }
}
