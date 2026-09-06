#include <iostream>
#include <stdexcept>
#include <string>
#include "keyboardroutingpolicy.h"
using P = KeyboardRoutingPolicy;
static void check(bool ok, const char *message) { if (!ok) { throw std::runtime_error(message); } }
int main() {
    int passed = 0;
    const auto run = [&passed](const char *name, void (*test)()) {
        try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception &e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    run("unmapped_uhid_pair", [](){ P p;check(p.dispatch(1,P::Press,false,65,P::Uhid).route==P::Uhid,"press");check(p.dispatch(1,P::Release,false,65,P::Converter).route==P::Uhid,"release owner");check(p.heldCount()==0,"cleared"); });
    run("mapped_converter_pair", [](){ P p;check(p.dispatch(1,P::Press,false,87,P::Converter).route==P::Converter,"press");check(p.dispatch(1,P::Release,false,87,P::Uhid).route==P::Converter,"release owner"); });
    run("logical_layout_change", [](){ P p;p.dispatch(7,P::Press,false,87,P::Converter);auto d=p.dispatch(7,P::Release,false,81,P::Uhid);check(d.route==P::Converter&&d.logicalKey==87,"logical key must remain W"); });
    run("duplicate_down", [](){ P p;p.dispatch(1,P::Press,false,65,P::Uhid);check(p.dispatch(1,P::Press,false,65,P::Uhid).route==P::Ignore,"duplicate");check(p.heldCount()==1,"one owner"); });
    run("repeat_press", [](){ P p;p.dispatch(1,P::Press,false,65,P::Uhid);check(p.dispatch(1,P::Press,true,65,P::Converter).route==P::Uhid,"repeat owner"); });
    run("repeat_release", [](){ P p;p.dispatch(1,P::Press,false,65,P::Uhid);check(p.dispatch(1,P::Release,true,65,P::Converter).route==P::Uhid,"repeat up owner");check(p.heldCount()==1,"repeat must not release ownership"); });
    run("orphan_repeat", [](){ P p;check(p.dispatch(1,P::Press,true,65,P::Uhid).route==P::Ignore,"no resurrect");check(p.heldCount()==0,"empty"); });
    run("orphan_up", [](){ P p;check(p.dispatch(1,P::Release,false,65,P::Converter).route==P::Ignore,"orphan release"); });
    run("reset_blocks_old_repeat_and_up", [](){ P p;p.dispatch(1,P::Press,false,65,P::Uhid);p.clear();check(p.dispatch(1,P::Press,true,65,P::Uhid).route==P::Ignore,"old repeat");check(p.dispatch(1,P::Release,false,65,P::Converter).route==P::Ignore,"old up"); });
    run("reset_allows_new_real_down", [](){ P p;p.dispatch(1,P::Press,false,65,P::Uhid);p.clear();check(p.dispatch(1,P::Press,false,65,P::Uhid).route==P::Uhid,"new press"); });
    run("release_uhid_preserves_mapped_hold", [](){ P p;p.dispatch(1,P::Press,false,65,P::Uhid);p.dispatch(2,P::Press,false,87,P::Converter);p.clearUhid();check(p.heldCount()==1,"only mapped remains");check(p.dispatch(1,P::Release,false,65,P::Uhid).route==P::Ignore,"old hid up");check(p.dispatch(2,P::Release,false,87,P::Uhid).route==P::Converter,"mapped up"); });
    run("left_right_modifier_ids", [](){ P p;p.dispatch(0x800000000000001dULL,P::Press,false,100,P::Uhid);p.dispatch(0x800000000000011dULL,P::Press,false,100,P::Uhid);check(p.heldCount()==2,"physical distinction");p.dispatch(0x800000000000001dULL,P::Release,false,100,P::Converter);check(p.heldCount()==1,"other modifier remains"); });
    run("concurrent_inputs", [](){ P p;for(std::uint64_t i=1;i<=32;++i)p.dispatch(i,P::Press,false,int(i),i%2?P::Uhid:P::Converter);check(p.heldCount()==32,"all holds");for(std::uint64_t i=1;i<=32;++i)check(p.dispatch(i,P::Release,false,int(i),P::Ignore).route==(i%2?P::Uhid:P::Converter),"correct owner");check(p.heldCount()==0,"all released"); });
    run("irrelevant_event", [](){ P p;check(p.dispatch(1,P::Other,false,65,P::Uhid).route==P::Ignore,"other event");check(p.dispatch(1,P::Other,true,65,P::Uhid).route==P::Ignore,"other repeat");check(p.heldCount()==0,"no state"); });
    std::cout << "RESULT " << passed << "/14\n";
    return passed==14?0:1;
}
