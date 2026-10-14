#include "game/asylum/opening_movies.h"
#include <cstdio>
int main(){
    using bvr::aa::startup::opening_movie_name;
    for(auto name:{L"baa_logo_run_v5_h264",L"BAA_LOGO_RUN_V5_H264.BIK",L"..\\Movies\\utlogo.bik",L"Legal",L"Legalus.bik",L"Black.bik"})
        if(!opening_movie_name(name))return 1;
    for(auto name:{L"101_Batmobile_Intro.bik",L"102_Warden_intro",L"baa_logo_run_v5_h264_extra.bik",L"Credits.bik",L"Loading.bik",L"",L"not_black"})
        if(opening_movie_name(name))return 2;
    std::puts("PASS: Unicode engine startup basenames and Bink paths match; story/loading/unknown movies excluded.");
}
