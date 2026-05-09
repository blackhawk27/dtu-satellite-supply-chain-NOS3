#ifndef NOS3_GENERIC_TT_C42DATAPROVIDER_HPP
#define NOS3_GENERIC_TT_C42DATAPROVIDER_HPP

#include <boost/property_tree/ptree.hpp>
#include <ItcLogger/Logger.hpp>
#include <generic_tt_c_data_point.hpp>
#include <sim_data_42socket_provider.hpp>

namespace Nos3
{
    /* Standard for a 42 data provider */
    class Generic_tt_c42DataProvider : public SimData42SocketProvider
    {
    public:
        /* Constructors */
        Generic_tt_c42DataProvider(const boost::property_tree::ptree& config);

        /* Accessors */
        boost::shared_ptr<SimIDataPoint> get_data_point(void) const;

    private:
        /* Disallow these */
        ~Generic_tt_c42DataProvider(void) {};
        Generic_tt_c42DataProvider& operator=(const Generic_tt_c42DataProvider&) {return *this;};

        int16_t _sc;  /* Which spacecraft number to parse out of 42 data */
    };
}

#endif
