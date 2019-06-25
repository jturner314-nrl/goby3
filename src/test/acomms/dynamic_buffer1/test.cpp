// Copyright 2009-2018 Toby Schneider (http://gobysoft.org/index.wt/people/toby)
//                     GobySoft, LLC (2013-)
//                     Massachusetts Institute of Technology (2007-2014)
//
//
// This file is part of the Goby Underwater Autonomy Project Binaries
// ("The Goby Binaries").
//
// The Goby Binaries are free software: you can redistribute them and/or modify
// them under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// The Goby Binaries are distributed in the hope that they will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Goby.  If not, see <http://www.gnu.org/licenses/>.

#define BOOST_TEST_MODULE dynamic_buffer_test

#include <boost/test/included/unit_test.hpp>

#include "goby/acomms/buffer/dynamic_buffer.h"
#include "goby/time/io.h"

bool close_enough(double a, double b, int precision)
{
    return std::abs(a - b) < std::pow(10, -precision);
}

struct GLogSetup
{
    GLogSetup()
    {
        goby::glog.add_stream(goby::util::logger::DEBUG3, &std::cerr);
        goby::glog.set_name("test");
    }

    ~GLogSetup() {}
};

BOOST_GLOBAL_FIXTURE(GLogSetup);

BOOST_AUTO_TEST_CASE(check_single_configuration)
{
    {
        goby::acomms::protobuf::DynamicBufferConfig cfg1;
        goby::acomms::DynamicSubBuffer<std::string> buffer(cfg1);
        BOOST_CHECK_MESSAGE(cfg1.SerializeAsString() == buffer.cfg().SerializeAsString(),
                            "Expected " << cfg1.ShortDebugString()
                                        << ", got: " << buffer.cfg().ShortDebugString());
    }

    {
        goby::acomms::protobuf::DynamicBufferConfig cfg1;
        cfg1.set_ack_required(false);
        cfg1.set_ttl(2000);
        cfg1.set_value_base(10);
        cfg1.set_max_queue(5);

        goby::acomms::DynamicSubBuffer<std::string> buffer(cfg1);
        BOOST_CHECK_MESSAGE(cfg1.SerializeAsString() == buffer.cfg().SerializeAsString(),
                            "Expected " << cfg1.ShortDebugString()
                                        << ", got: " << buffer.cfg().ShortDebugString());
    }
}

BOOST_AUTO_TEST_CASE(check_multi_configuration)
{
    goby::acomms::protobuf::DynamicBufferConfig cfg1;
    cfg1.set_ack_required(false);
    cfg1.set_ttl(2000);
    cfg1.set_value_base(10);
    cfg1.set_max_queue(5);

    goby::acomms::protobuf::DynamicBufferConfig cfg2;
    cfg2.set_ack_required(true);
    cfg2.set_ttl(3000);
    cfg2.set_value_base(20);
    cfg2.set_max_queue(10);
    cfg2.set_newest_first(false);

    goby::acomms::protobuf::DynamicBufferConfig expected_cfg;
    expected_cfg.set_ack_required(true);
    expected_cfg.set_ttl(2500);
    expected_cfg.set_value_base(15);
    expected_cfg.set_max_queue(10);
    expected_cfg.set_newest_first(false);

    goby::acomms::DynamicSubBuffer<std::string> buffer({cfg1, cfg2});
    BOOST_CHECK_MESSAGE(expected_cfg.SerializeAsString() == buffer.cfg().SerializeAsString(),
                        "Expected " << expected_cfg.ShortDebugString()
                                    << ", got: " << buffer.cfg().ShortDebugString());
}

BOOST_AUTO_TEST_CASE(check_top_value)
{
    goby::acomms::protobuf::DynamicBufferConfig cfg;

    // should be priority value of 1.0 after 10 ms
    cfg.set_ttl(10);
    cfg.set_value_base(1000);

    goby::acomms::DynamicSubBuffer<std::string> buffer(cfg);
    BOOST_CHECK_EQUAL(buffer.top_value(), -std::numeric_limits<double>::infinity());

    buffer.push("foo");

    BOOST_CHECK(!buffer.empty());

    for (int i = 1, n = 3; i <= n; ++i)
    {
        // reset last access
        buffer.top();
        int us_dt = i * 10000; // 10*i ms
        usleep(us_dt);
        double v = buffer.top_value();
        BOOST_CHECK_MESSAGE(close_enough(v, i * 1.0, 0), "Expected " << i * 1.0 << ", got: " << v);
    }

    goby::time::SimulatorSettings::using_sim_time = true;
    goby::time::SimulatorSettings::warp_factor = 2;

    buffer.top();
    int us_dt = 10000; // 10 ms
    usleep(us_dt);
    double v = buffer.top_value();
    BOOST_CHECK_MESSAGE(close_enough(v, 2 * 1.0, 0), "Expected " << 2 * 1.0 << ", got: " << v);

    goby::time::SimulatorSettings::using_sim_time = false;
}

BOOST_AUTO_TEST_CASE(check_order)
{
    {
        goby::acomms::protobuf::DynamicBufferConfig cfg;
        cfg.set_newest_first(true);
        goby::acomms::DynamicSubBuffer<std::string> buffer(cfg);

        buffer.push("first");
        buffer.push("second");

        BOOST_CHECK_EQUAL(buffer.top().second, "second");
        buffer.pop();
        BOOST_CHECK_EQUAL(buffer.top().second, "first");
    }

    {
        goby::acomms::protobuf::DynamicBufferConfig cfg;
        cfg.set_newest_first(false);
        goby::acomms::DynamicSubBuffer<std::string> buffer(cfg);

        buffer.push("first");
        buffer.push("second");

        BOOST_CHECK_EQUAL(buffer.top().second, "first");
        buffer.pop();
        BOOST_CHECK_EQUAL(buffer.top().second, "second");
    }
}

BOOST_AUTO_TEST_CASE(check_subbuffer_expire)
{
    for (bool newest_first : {false, true})
    {
        goby::acomms::protobuf::DynamicBufferConfig cfg;
        using boost::units::si::milli;
        using boost::units::si::seconds;

        cfg.set_ttl_with_units(10.0 * milli * seconds);
        cfg.set_newest_first(newest_first);

        goby::acomms::DynamicSubBuffer<std::string> buffer(cfg);
        buffer.push("first");
        BOOST_CHECK_EQUAL(buffer.size(), 1);
        usleep(5000); // 5 ms
        buffer.push("second");
        BOOST_CHECK_EQUAL(buffer.size(), 2);
        usleep(5000); // 5 ms
        auto exp1 = buffer.expire();
        BOOST_CHECK_EQUAL(buffer.size(), 1);
        usleep(5000); // 5 ms
        auto exp2 = buffer.expire();

        BOOST_CHECK(buffer.empty());
        BOOST_REQUIRE_EQUAL(exp1.size(), 1);
        BOOST_REQUIRE_EQUAL(exp1[0].second, "first");
        BOOST_REQUIRE_EQUAL(exp2.size(), 1);
        BOOST_REQUIRE_EQUAL(exp2[0].second, "second");
    }
}

struct DynamicBufferFixture
{
    DynamicBufferFixture()
    {
        using boost::units::si::milli;
        using boost::units::si::seconds;
        goby::acomms::protobuf::DynamicBufferConfig cfg1;
        cfg1.set_ack_required(false);
        cfg1.set_ttl_with_units(10.0 * milli * seconds);
        cfg1.set_value_base(10);
        cfg1.set_max_queue(2);
        cfg1.set_newest_first(true);
        buffer.create("A", cfg1);

        goby::acomms::protobuf::DynamicBufferConfig cfg2;
        cfg2.set_ack_required(true);
        cfg2.set_ttl_with_units(10.0 * milli * seconds);
        cfg2.set_value_base(10);
        cfg2.set_max_queue(2);
        cfg2.set_newest_first(false);
        buffer.create("B", cfg2);
    }

    ~DynamicBufferFixture() {}

    goby::acomms::DynamicBuffer<std::string> buffer;
};

BOOST_FIXTURE_TEST_CASE(create_buffer, DynamicBufferFixture)
{
    BOOST_CHECK(buffer.empty());
    BOOST_CHECK_EQUAL(buffer.size(), 0);

    buffer.push("A", "first");

    auto vp = buffer.top();
    BOOST_CHECK_EQUAL(std::get<0>(vp), "A");
    BOOST_CHECK_EQUAL(std::get<2>(vp), "first");

    BOOST_CHECK(buffer.erase(vp));
    BOOST_CHECK(buffer.empty());
}

BOOST_FIXTURE_TEST_CASE(two_subbuffer_contest, DynamicBufferFixture)
{
    auto now = goby::time::SteadyClock::now();

    buffer.push({"A", now, "1"});
    buffer.push({"B", now, "1"});
    buffer.push({"A", now, "2"});
    buffer.push({"B", now, "2"});

    // will be "A" because it was created first (and last access is initialized to creation time)
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(std::get<0>(vp), "A");
        BOOST_CHECK_EQUAL(std::get<2>(vp), "2");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 3);
    }

    // now it will be "B"
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(std::get<0>(vp), "B");
        BOOST_CHECK_EQUAL(std::get<2>(vp), "1");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 2);
    }

    // A
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(std::get<0>(vp), "A");
        BOOST_CHECK_EQUAL(std::get<2>(vp), "1");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 1);
    }

    // B
    {
        auto vp = buffer.top();
        BOOST_CHECK_EQUAL(std::get<0>(vp), "B");
        BOOST_CHECK_EQUAL(std::get<2>(vp), "2");
        BOOST_CHECK(buffer.erase(vp));
        BOOST_CHECK_EQUAL(buffer.size(), 0);
    }
}

BOOST_FIXTURE_TEST_CASE(arbitrary_erase, DynamicBufferFixture)
{
    auto now = goby::time::SteadyClock::now();

    buffer.push({"A", now, "1"});
    buffer.push({"B", now, "1"});
    buffer.push({"A", now, "2"});
    buffer.push({"B", now, "2"});

    BOOST_CHECK_EQUAL(buffer.size(), 4);
    BOOST_CHECK(buffer.erase({"A", now, "1"}));
    BOOST_CHECK_EQUAL(buffer.size(), 3);
    BOOST_CHECK(buffer.erase({"A", now, "2"}));
    BOOST_CHECK_EQUAL(buffer.size(), 2);
    BOOST_CHECK(buffer.erase({"B", now, "1"}));
    BOOST_CHECK_EQUAL(buffer.size(), 1);
    BOOST_CHECK(buffer.erase({"B", now, "2"}));
    BOOST_CHECK_EQUAL(buffer.size(), 0);
}

BOOST_FIXTURE_TEST_CASE(check_expire, DynamicBufferFixture)
{
    auto now = goby::time::SteadyClock::now();
    buffer.push({"A", now, "first"});
    buffer.push({"B", now, "first"});
    BOOST_CHECK_EQUAL(buffer.size(), 2);
    buffer.push({"A", now + std::chrono::milliseconds(5), "second"});
    buffer.push({"B", now + std::chrono::milliseconds(5), "second"});
    BOOST_CHECK_EQUAL(buffer.size(), 4);
    usleep(10000); // 10 ms
    auto exp1 = buffer.expire();
    BOOST_CHECK_EQUAL(buffer.size(), 2);
    usleep(5000); // 5 ms
    auto exp2 = buffer.expire();

    BOOST_CHECK(buffer.empty());
    BOOST_REQUIRE_EQUAL(exp1.size(), 2);
    BOOST_REQUIRE_EQUAL(std::get<2>(exp1[0]), "first");
    BOOST_REQUIRE_EQUAL(std::get<2>(exp1[1]), "first");
    BOOST_REQUIRE_EQUAL(exp2.size(), 2);
    BOOST_REQUIRE_EQUAL(std::get<2>(exp2[0]), "second");
    BOOST_REQUIRE_EQUAL(std::get<2>(exp2[1]), "second");
}

BOOST_FIXTURE_TEST_CASE(check_max_queue, DynamicBufferFixture)
{
    auto now = goby::time::SteadyClock::now();

    BOOST_CHECK_EQUAL(buffer.push({"A", now, "1"}).size(), 0);
    BOOST_CHECK_EQUAL(buffer.push({"A", now, "2"}).size(), 0);
    BOOST_CHECK_EQUAL(buffer.push({"B", now, "1"}).size(), 0);
    BOOST_CHECK_EQUAL(buffer.push({"B", now, "2"}).size(), 0);

    // newest first = true pushes out oldest
    {
        auto exceeded = buffer.push({"A", now, "3"});
        BOOST_CHECK_EQUAL(exceeded.size(), 1);
        BOOST_CHECK_EQUAL(std::get<0>(exceeded[0]), "A");
        BOOST_CHECK_EQUAL(std::get<1>(exceeded[0]), now);
        BOOST_CHECK_EQUAL(std::get<2>(exceeded[0]), "1");
    }

    // newest first = false pushes out newest (value just pushed)
    {
        auto exceeded = buffer.push({"B", now, "3"});

        // newest first pushes out oldest
        BOOST_CHECK_EQUAL(exceeded.size(), 1);
        BOOST_CHECK_EQUAL(std::get<0>(exceeded[0]), "B");
        BOOST_CHECK_EQUAL(std::get<1>(exceeded[0]), now);
        BOOST_CHECK_EQUAL(std::get<2>(exceeded[0]), "3");
    }
}
