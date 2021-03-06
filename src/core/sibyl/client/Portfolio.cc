/*
   Copyright 2017 Hosang Yoon

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "Portfolio.h"

#include <iostream>
#include <cinttypes>
#include <cstring>
#include <iomanip>
#include <cmath>
#include <sstream>

#include "../util/CandlePlot.h"
#include "../ostream_format.h"

namespace sibyl
{

const std::vector<ItemState>& Portfolio::GetStateVec()
{
    if (items.size() != vecState.size()) vecState.resize(items.size());

    std::size_t codeIdx = 0;
    for (const auto &code_pItem : items)
    {
        const auto &i     = *code_pItem.second; 
              auto &state = vecState[codeIdx];
        state.code = code_pItem.first; 
        state.time = time;
        state.pr   = i.pr;
        state.qr   = i.qr;
        state.tbr  = i.tbr;
        
        if (i.Type() == SecType::ELW)
        {
            const auto &i = *static_cast<ELW<ItemPf>*>(code_pItem.second.get()); // reference as ELW<ItemPf>
            state.isELW = true;
            state.iCP = (i.CallPut() == OptType::call) - (i.CallPut() == OptType::put);
            state.expiry = i.Expiry(); 
            state.kospi200 = ELW<ItemPf>::kospi200;
            state.thr = i.thr;
        } else
            state.isELW = false;
        
        if (i.Type() == SecType::ETF)
        {
            const auto &i = *static_cast<ETF<ItemPf>*>(code_pItem.second.get()); // reference as ETF<ItemPf>
            state.isETF = true;
            state.devNAV = i.devNAV;
        } else
            state.isETF = false;
        
        codeIdx++;
    }
    return vecState;
}

void Portfolio::SetStateLogPaths(CSTR &state, CSTR &log)
{
    pathState = state;
    if ((pathState.empty() == false) && ('/' != pathState[pathState.size() - 1])) pathState.append("/");
    pathLog   = log;
    if ((pathLog.empty()   == false) && ('/' != pathLog  [pathLog.size()   - 1])) pathLog.append("/");
    
    if (pathLog.empty() == false)
    {
        logMsgIn.open (pathLog + "msg_in.log" , std::ofstream::trunc);
        logVecOut.open(pathLog + "vec_out.log", std::ofstream::trunc);
        if ((logMsgIn.is_open() == false) || (logVecOut.is_open() == false))
            std::cerr << "Portfolio: log path not accessible" << std::endl;
    }
}

int Portfolio::ApplyMsgIn(char *msg) // Parse message and update entries
{    
    if (logMsgIn.is_open() == true) // log raw input from server
    {
        int timeMsg;
        sscanf(msg + 5, "%d", &timeMsg); // msg starts with "/*\nb [time]"
        logMsgIn << "[t=" << timeMsg << "]\n"; 
        logMsgIn.write(msg, (std::streamsize)strlen(msg));
    }
    
    auto iM = std::end(items);
    for (char *pcLine = strtok(msg, "\n"); pcLine != nullptr; pcLine = strtok(nullptr, "\n"))
    {
        char *pc = strpbrk(pcLine, "\r\n");
        if (pc != NULL) *pc = '\0';
        if (strlen(pcLine) == 0) continue;
        
        if (pcLine[0] == 'b')
        {
            // char cW : index of word (warning: overflows at 127)
            for (char *pcWord = strchr(pcLine, ' '), cW = 1; pcWord != nullptr; pcWord = strchr(pcWord, ' '), cW++)
            {
                while (*pcWord == ' ') pcWord++;
                if (cW == 1) {
                    int t;
                    sscanf(pcWord, "%d", &t);
                    time = t; // std::atomic_int time
                }
                if (cW == 2) sscanf(pcWord, "%" SCNd64, &bal);
                if (cW == 3) sscanf(pcWord, "%" SCNd64, &sum.buy);
                if (cW == 4) sscanf(pcWord, "%" SCNd64, &sum.sell);
                if (cW == 5) sscanf(pcWord, "%" SCNd64, &sum.feetax);
            }
        }
        if (pcLine[0] == 's')
        {
            for (char *pcWord = strchr(pcLine, ' '), cW = 1; pcWord != nullptr; pcWord = strchr(pcWord, ' '), cW++)
            {
                while (*pcWord == ' ') pcWord++;
                if ((cW - 1) % 3 == 0) sscanf(pcWord, "%" SCNd64, &sum.tck_orig[(std::size_t)((cW - 1) / 3)].bal);
                if ((cW - 1) % 3 == 1) sscanf(pcWord, "%" SCNd64, &sum.tck_orig[(std::size_t)((cW - 1) / 3)].q  );
                if ((cW - 1) % 3 == 2) sscanf(pcWord, "%" SCNd64, &sum.tck_orig[(std::size_t)((cW - 1) / 3)].evt);
            }
        }
        if (pcLine[0] == 'k')
        {
            for (char *pcWord = strchr(pcLine, ' '), cW = 1; pcWord != nullptr; pcWord = strchr(pcWord, ' '), cW++)
            {
                while (*pcWord == ' ') pcWord++;
                if (cW == 1) sscanf(pcWord, "%f", &ELW<ItemPf>::kospi200);
            }
        }
        if (pcLine[0] == 'd')
        {
            iM = std::end(items);
            for (char *pcWord = strchr(pcLine, ' '), cW = 1; pcWord != nullptr; pcWord = strchr(pcWord, ' '), cW++)
            {
                while (*pcWord == ' ') pcWord++;
                if (cW == 1)
                {
                    char *pcSpace = strchr(pcWord, ' ');
                    if (pcSpace != nullptr) *pcSpace = '\0';
                    STR c(pcWord);
                    if (pcSpace != nullptr) *pcSpace = ' ';
                    
                    iM = items.find(c);
                    if (iM == std::end(items))
                    {
                        auto it_bool = items.insert(std::make_pair(c, std::unique_ptr<ItemPf>(new KOSPI<ItemPf>)));
                        verify(it_bool.second == true); // assure successful insertion
                        iM = it_bool.first;
                    }
                }
                auto &i = *(iM->second); // reference to ItemPf
                if  (cW ==  2)               sscanf(pcWord, "%f"      , &i.pr);
                if  (cW ==  3)               sscanf(pcWord, "%" SCNd64, &i.qr);
                if ((cW >=  4) && (cW < 24)) sscanf(pcWord, "%d"      , &i.tbr[(std::size_t)(cW -  4)].p);
                if ((cW >= 24) && (cW < 44)) sscanf(pcWord, "%d"      , &i.tbr[(std::size_t)(cW - 24)].q);
            }
        }
        if (pcLine[0] == 'e')
        {
            if (iM->second->Type() == SecType::KOSPI) // on first run: reallocate as ELW
            {
                auto &ptr = iM->second;
                KOSPI<ItemPf> temp = *static_cast<KOSPI<ItemPf>*>(ptr.get()); // store copy
                ptr.reset(new ELW<ItemPf>);
                ptr->pr   = temp.pr;
                ptr->qr   = temp.qr;
                ptr->tbr  = temp.tbr;
            }
            auto &i = *static_cast<ELW<ItemPf>*>(iM->second.get()); // reference as ELW<ItemPf>
            
            OptType optType(OptType::null);
            int expiry = -1;
            int iCP = 0;
            for (char *pcWord = strchr(pcLine, ' '), cW = 1; pcWord != nullptr; pcWord = strchr(pcWord, ' '), cW++)
            {
                while (*pcWord == ' ') pcWord++;
                if  (cW ==  2)               sscanf(pcWord, "%d", &iCP);
                if  (cW ==  3)               sscanf(pcWord, "%d", &expiry);
                if ((cW >=  4) && (cW < 12)) sscanf(pcWord, "%f", &i.thr[(std::size_t)(cW - 4)]);
            }
            if (iCP == +1) optType = OptType::call;
            if (iCP == -1) optType = OptType::put;
            i.SetInfo(optType, expiry);
        }
        if (pcLine[0] == 'n')
        {
            if (iM->second->Type() == SecType::KOSPI) // on first run: reallocate as ETF
            {
                auto &ptr = iM->second;
                KOSPI<ItemPf> temp = *static_cast<KOSPI<ItemPf>*>(ptr.get()); // store copy
                ptr.reset(new ETF<ItemPf>);
                ptr->pr   = temp.pr;
                ptr->qr   = temp.qr;
                ptr->tbr  = temp.tbr;
            }
            auto &i = *static_cast<ETF<ItemPf>*>(iM->second.get()); // reference as ETF<ItemPf>
            
            for (char *pcWord = strchr(pcLine, ' '), cW = 1; pcWord != nullptr; pcWord = strchr(pcWord, ' '), cW++)
            {
                while (*pcWord == ' ') pcWord++;
                if  (cW ==  2)               sscanf(pcWord, "%f", &i.devNAV);
            }
        }
        if (pcLine[0] == 'o')
        {
            auto &i = *(iM->second); // reference to ItemPf
            i.ord.clear();
            OrderPf o;
            for (char *pcWord = strchr(pcLine, ' '), cW = 1; pcWord != nullptr; pcWord = strchr(pcWord, ' '), cW++)
            {
                while (*pcWord == ' ') pcWord++;
                if  (cW == 2)                 sscanf(pcWord, "%d", &i.cnt);
                if ((cW >= 3) &&  (cW & 0x1)) sscanf(pcWord, "%d", &o.p);
                if ((cW >= 3) && !(cW & 0x1))
                {
                    sscanf(pcWord, "%d", &o.q);
                    verify(o.q != 0);
                    if      (o.q > 0)
                        o.type = OrdType::buy;
                    else if (o.q < 0) {
                        o.type = OrdType::sell;
                        o.q = -o.q;
                    }
                    i.ord.insert(std::make_pair(o.p, o));
                }
            }
        }
    }
    
    UpdateRefInitBal();
    
    if (pathState.empty() == false) {
        WriteState();
    }

    if (logVecOut.is_open() == true) // log t, pr, qr, tbr values
    {
        logVecOut << "[t=" << time << "]\n";
        for (const auto &code_pItem : items)
        {
            ItemPf &i = *code_pItem.second;
            sprintf(bufLine, "{%s}\n"               , code_pItem.first.c_str()); logVecOut << bufLine;
            sprintf(bufLine, "t\t%10d\n"            , time.load());              logVecOut << bufLine;
            sprintf(bufLine, "pr\t%.4e\n"           , i.pr);                     logVecOut << bufLine;
            sprintf(bufLine, "qr\t%10" PRId64 "\n"  , i.qr);                     logVecOut << bufLine;
            sprintf(bufLine, "     \ttbpr\t\ttbqr\n");                           logVecOut << bufLine;
            for (std::ptrdiff_t idx = 0; idx < idx::szTb; idx++)
            {
                sprintf(bufLine, "[%s%2d]\t%10d\t%10d\n", (idx <= idx::ps1 ? "s" : "b"), (idx <= idx::ps1 ? (int)(idx::ps1 - idx + 1) : (int)(idx - idx::pb1 + 1)), i.tbr[(std::size_t)idx].p, i.tbr[(std::size_t)idx].q);
                logVecOut << bufLine;
            }
        }
    }
    
    if (time >= kTimeBounds::end) return -1;
    return 0;
}

void Portfolio::WriteState()
{
    if (pathState.empty() == true) return;
    
    STR filename(pathState);
    if (time <= kTimeBounds::init)
        filename.append("client_ini.log");
    else if (time >= kTimeBounds::end - 60)
        filename.append("client_fin.log");
    else
        filename.append("client_cur.log");
    
    static const std::size_t size= 1 << 10;
    static char  buf[size];
    const int  nItemPerLine = 4;
    const char itemSpacer[] = "        ";
    
    std::ofstream ofs(filename);
    if (ofs.is_open() == true)
    {
        SEval se = Evaluate();

        constexpr std::size_t colHeight = 10;
        auto topCnts = GetTopCnts(2 * colHeight); // returns iterators for up to top 20 items sorted by cnt * ps0
        auto GetNthCnt = [&](std::size_t n) {
            if (n >= topCnts.size())
                return STR();
            else {
                std::stringstream ss;
                ss << fmt_code(topCnts[n]->first) << ' ' << std::setw(2)
                   << (int) (std::round((double) 100 * topCnts[n]->second->cnt * topCnts[n]->second->Ps0() / se.evalTot))
                   << "%";
                return ss.str();
            } 
        };
        auto FillLeft = [](std::ofstream &ofs, const char *buf) {
            constexpr std::size_t leftColWidth = 50;
            ofs << buf << std::setw(leftColWidth - strlen(buf)) << "";
        };
        auto FillRight = [&](std::ofstream &ofs, std::size_t n) {
            ofs << GetNthCnt(n).c_str() << "    " << GetNthCnt(n + colHeight).c_str() << '\n';
        };

        int timeCur = time; // std::atomic_int
        
        sprintf(buf, "t = %5d sec", timeCur);
        FillLeft(ofs, buf); ofs << (topCnts.size() > 0 ? "top\n" : "\n");

        sprintf(buf, "  bal  u  %12" PRId64, se.balU   );
        FillLeft(ofs, buf); FillRight(ofs, 0);

        sprintf(buf, "  bal b_o %12" PRId64, se.balBO  );
        FillLeft(ofs, buf); FillRight(ofs, 1);

        sprintf(buf, "  evl cnt %12" PRId64, se.evalCnt);
        FillLeft(ofs, buf); FillRight(ofs, 2);

        sprintf(buf, "  evl s_o %12" PRId64, se.evalSO );
        FillLeft(ofs, buf); FillRight(ofs, 3);

        sprintf(buf, "  evl tot %12" PRId64 " (r%+.2f%%) (s%+.2f%%)", se.evalTot,
            ((double) se.evalTot / balRef - 1.0) * 100.0, ((double) se.evalTot / balInit - 1.0) * 100.0);
        FillLeft(ofs, buf); FillRight(ofs, 4);

        // sprintf(buf, "   sum  b  %12" PRId64 "\n" , sum.buy   ); ofs << buf;
        // sprintf(buf, "   sum  s  %12" PRId64 "\n" , sum.sell  ); ofs << buf;
        buf[0] = '\0';
        FillLeft(ofs, buf); FillRight(ofs, 5);
        
        sprintf(buf, "sum [t_o]          bal    quant      evt");
        FillLeft(ofs, buf); FillRight(ofs, 6);

        // for (std::size_t idx = idx::ps1; idx <= idx::pb1; idx++)
        // {
        //     sprintf(buf, "     [%s%2d] ", ((int)idx <= idx::ps1 ? "s" : "b"),
        //         (idx <= idx::ps1 ? (int)idx::ps1 - (int)idx + 1 : (int)idx - (int)idx::pb1 + 1)); ofs << buf;
        //     sprintf(buf, "%12" PRId64 " %8" PRId64 " %8" PRId64 "\n",
        //         sum.tck_orig[idx].bal, sum.tck_orig[idx].q, sum.tck_orig[idx].evt); ofs << buf;
        //     if (idx == idx::ps1)
        //     {
        //         taken out below
        //     }
        // }
        sprintf(buf, "    [s 0] %12" PRId64 " %8" PRId64 " %8" PRId64,
            sum.tck_orig[idxTckOrigS0].bal, sum.tck_orig[idxTckOrigS0].q, sum.tck_orig[idxTckOrigS0].evt);
        FillLeft(ofs, buf); FillRight(ofs, 7);

        sprintf(buf, "    [b 0] %12" PRId64 " %8" PRId64 " %8" PRId64,
            sum.tck_orig[idxTckOrigB0].bal, sum.tck_orig[idxTckOrigB0].q, sum.tck_orig[idxTckOrigB0].evt);
        FillLeft(ofs, buf); FillRight(ofs, 8);

        sprintf(buf, "    [f+t] %12" PRId64, sum.feetax);
        FillLeft(ofs, buf); FillRight(ofs, 9);

        ofs << '\n';

        static std::vector<float> tot_s, u_tot, index;
        static float index_init = std::nan("");

        if (timeCur == 0) {
            tot_s.clear();
            u_tot.clear();
            index.clear();
        }

        // if (timeCur >= 0 && timeCur <= kTimeBounds::stop && timeCur % 300 == 0) { // every 5 min

        if (timeCur >= kTimeBounds::init && timeCur <= kTimeBounds::stop + 40 * 60) { 
            tot_s.push_back((float) ((double) se.evalTot / balInit - 1.0) * 100.0);
            u_tot.push_back((float) ((double) se.balU / se.evalTot));

#ifndef __linux__
            if (std::isnan(index_init) == true)
#else // g++'s std::isnan is defective under -ffast-math
            if (isnanf(index_init) == true)
#endif /* !__linux__ */
            {
                index.push_back(0.0f);
#ifndef __linux__
                if (std::isnan(ELW<ItemPf>::kospi200) == false)
#else // g++'s std::isnan is defective under -ffast-math
                if (isnanf(ELW<ItemPf>::kospi200) == false)
#endif /* !__linux__ */
                    index_init = ELW<ItemPf>::kospi200; // store the first non-nan value
            } else
                index.push_back((float) ((double) ELW<ItemPf>::kospi200 / index_init - 1.0) * 100.0);
        }
        
        // v should not contain nan's
        auto FindRange = [](const std::vector<float> &v) {
            int range = 1;
            if (v.empty() == false) {
                float maxval = std::max(std::fabs(*std::max_element(std::begin(v), std::end(v))),
                                        std::fabs(*std::min_element(std::begin(v), std::end(v))));
                if      (maxval > 2.0f) range = 5;
                else if (maxval > 1.0f) range = 2;
            }
            return range;
        };

        int rng_r = FindRange(tot_s);
        int rng_i = FindRange(index);

        std::size_t bin_size = 300 / kTimeRates::secPerTick; // 5 min

        ofs << "─────┬─────┰─────┬─────┰─────┬─────┰─────┬─────┰─────┬─────┰─────┬─────┰─────┬─\n"
            << CandlePlot(u_tot, 11, 0.0f, 1.0f, bin_size, "u / tot (0, 1)") << '\n';

        std::string title_r = "rate_s (-" + std::to_string(rng_r) + "%, +" + std::to_string(rng_r) + "%)"; 
        ofs << "─────┼─────╂─────┼─────╂─────┼─────╂─────┼─────╂─────┼─────╂─────┼─────╂─────┼─\n"
            << CandlePlot(tot_s, 21, (float) -rng_r, (float) rng_r, bin_size, title_r) << '\n';

        std::string title_i = "index (-" + std::to_string(rng_i) + "%, +" + std::to_string(rng_i) + "%)";
        ofs << "─────┼─────╂─────┼─────╂─────┼─────╂─────┼─────╂─────┼─────╂─────┼─────╂─────┼─\n"
            << CandlePlot(index, 21, (float) -rng_i, (float) rng_i, bin_size, title_i) << '\n';
            
        ofs << "─────┴─────┸─────┴─────┸─────┴─────┸─────┴─────┸─────┴─────┸─────┴─────┸─────┴─\n";
        ofs << '\n';
        
        ofs << "cnt\n";
        int iCnt = 0;    // number of items in current line
        int nOrdTot = 0; // total number of orders
        for (const auto &code_pItem : items)
        {
            const auto &i = *code_pItem.second;
            if (i.cnt > 0)
            {
                sprintf(buf, "      {%s} %8d (%6d)", code_pItem.first.c_str(), i.Ps0(), i.cnt); ofs << buf;
                if (nItemPerLine == ++iCnt)
                {
                    ofs << '\n';
                    iCnt = 0;
                }
                else
                    ofs << itemSpacer;
            }
            nOrdTot += i.ord.size();
        }
        if (iCnt != 0) ofs << '\n';
        ofs << '\n';
        
        auto ListOrder = [&](const OrdType &type) {
            int iCnt = 0;
            for (const auto &code_pItem : items)
            {
                const auto &i = *code_pItem.second;
                for (const auto &price_Order : i.ord)
                {
                    const auto &o = price_Order.second;
                    if (o.type == type)
                    {
                        int tck = i.P2Tck(o.p, o.type); // 0-based tick
                        if (tck == idx::tckN) tck = 98;     // display as 99 if not found
                        sprintf(buf, "[%s%2d] {%s} %8d (%6d)", (type == OrdType::buy ? "b" : "s"), tck + 1,
                            code_pItem.first.c_str(), o.p, o.q); ofs << buf;
                        if (nItemPerLine == ++iCnt)
                        {
                            ofs << '\n';
                            iCnt = 0;
                        }
                        else
                            ofs << itemSpacer;
                    }
                }
            }
            if (iCnt != 0) ofs << '\n';
        };
        
        sprintf(buf, "ord  [t_c] (%4d)\n", nOrdTot); ofs << buf;
        ListOrder(OrdType::buy) ;
        ListOrder(OrdType::sell);
        ofs << std::endl;
    }
    else
        std::cerr << "Portfolio: state path not accessible" << std::endl;
}

}
