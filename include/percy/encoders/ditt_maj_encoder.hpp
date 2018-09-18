#pragma once

#include <vector>
#include <kitty/kitty.hpp>
#include <abc/vecWec.h>
#include "encoder.hpp"

namespace percy
{
    class ditt_maj_encoder
    {
    private:
        static const int MAJ_NOBJS = 32;

        int level_dist[MAJ_NOBJS]; // How many steps are below a certain level
        int nr_levels; // The number of levels in the Boolean fence
        pabc::lit pLits[MAJ_NOBJS];
        pabc::Vec_Wec_t* vOutLits;
        solver_wrapper* solver;

        int varMarks[MAJ_NOBJS][3][MAJ_NOBJS];
        int varVals[MAJ_NOBJS]; // values of the PIs
        int iVar;

        static const int NR_SIM_TTS = 32;
        std::vector<kitty::dynamic_truth_table> sim_tts { NR_SIM_TTS };

        int find_fanin(const spec& spec, int i, int k)
        {
            int count = 0, iVar = -1;
            for (int j = 0; j < spec.nr_in + spec.nr_steps; j++) {
                if (varMarks[i][k][j] && solver->var_value(varMarks[i][k][j])) {
                    iVar = j;
                    count++;
                }
            }
            assert(count == 1);
            return iVar;
        }

    public:
        ditt_maj_encoder(solver_wrapper& solver)
        {
            this->solver = &solver;
            vOutLits = pabc::Vec_WecStart(MAJ_NOBJS);
        }

        ~ditt_maj_encoder()
        {
            pabc::Vec_WecFree(vOutLits);
        }

        void update_level_map(const spec& spec, const fence& f)
        {
            nr_levels = f.nr_levels();
            level_dist[0] = spec.get_nr_in();
            for (int i = 1; i <= nr_levels; i++) {
                level_dist[i] = level_dist[i - 1] + f.at(i - 1);
            }
        }

        int get_level(const spec& spec, int step_idx) const
        {
            int i = -1;
            // PIs are considered to be on level zero.
            if (step_idx < spec.nr_in) {
                return 0;
            } else if (step_idx == spec.nr_in) {
                // First step is always on level one
                return 1;
            }
            for (i = 0; i <= nr_levels; i++) {
                if (level_dist[i] > step_idx) {
                    break;
                }
            }
            return i;
        }

        int first_step_on_level(int level) const
        {
            if (level == 0) { return 0; }
            return level_dist[level-1];
        }

        void add_base_variables(const spec& spec)
        {
            iVar = 1;
            for (int k = 0; k < 3; k++) {
                const auto j = 2 - k;
                pabc::Vec_WecPush(vOutLits, j, pabc::Abc_Var2Lit(iVar, 0));
                varMarks[spec.nr_in][k][j] = iVar++;
            }
            for (int i = spec.nr_in + 1; i < spec.nr_in + spec.nr_steps; i++) {
                for (int k = 0; k < 3; k++) {
                    for (int j = 0; j < i - k; j++) {
                        pabc::Vec_WecPush(vOutLits, j, pabc::Abc_Var2Lit(iVar, 0));
                        varMarks[i][k][j] = iVar++;
                    }
                }
            }
            printf("The number of structural variables = %d\n", iVar);
        }

        void add_base_variables(const spec& spec, const fence& fence)
        {
            for (int i = 0; i < MAJ_NOBJS; i++) {
                pabc::Vec_IntClear(pabc::Vec_WecEntry(vOutLits, i));
            }

            iVar = 1;
            for (int k = 0; k < 3; k++) {
                const auto j = 2 - k;
                pabc::Vec_WecPush(vOutLits, j, pabc::Abc_Var2Lit(iVar, 0));
                varMarks[spec.nr_in][k][j] = iVar++;
            }
            for (int i = spec.nr_in + 1; i < spec.nr_in + spec.nr_steps; i++) {
                const auto level = get_level(spec, i);
                assert(level > 0);
                const auto first_z = first_step_on_level(level - 1);
                for (int z = first_z; z < first_step_on_level(level); z++) {
                    pabc::Vec_WecPush(vOutLits, z, pabc::Abc_Var2Lit(iVar, 0));
                    varMarks[i][0][z] = iVar++;
                }
                for (int k = 1; k < 3; k++) {
                    for (int j = 0; j < i - k; j++) {
                        pabc::Vec_WecPush(vOutLits, j, pabc::Abc_Var2Lit(iVar, 0));
                        varMarks[i][k][j] = iVar++;
                        //printf("varMarks[%d][%d][%d]=%d\n", i, k, j, iVar - 1);
                    }
                }
            }
            printf("The number of structural variables = %d\n", iVar);
        }

        void add_base_cnf(const spec& spec)
        {
            for (int i = 0; i < MAJ_NOBJS; i++) {
                pabc::Vec_IntClear(pabc::Vec_WecEntry(vOutLits, i));
            }

            int tmpLits[2];
            for (int i = spec.nr_in; i < spec.nr_in + spec.nr_steps; i++) {
                for (int k = 0; k < 3; k++) {
                    int nLits = 0;
                    for (int j = 0; j < spec.nr_in + spec.nr_steps; j++) {
                        if (varMarks[i][k][j]) {
                            pLits[nLits++] = pabc::Abc_Var2Lit(varMarks[i][k][j], 0);
                        }
                    }
                    if (!solver->add_clause(pLits, pLits + nLits))
                        assert(false);
                    for (int n = 0; n < nLits; n++) {
                        for (int m = n + 1; m < nLits; m++) {
                            tmpLits[0] = pabc::Abc_LitNot(pLits[n]);
                            tmpLits[1] = pabc::Abc_LitNot(pLits[m]);
                            if (!solver->add_clause(tmpLits, tmpLits + 2))
                                assert(false);
                        }
                    }
                    if (k == 2)
                        break;
                    // symmetry breaking
                    for (int j = 0; j < spec.nr_in + spec.nr_steps; j++) {
                        if (varMarks[i][k][j]) {
                            for (int n = j; n < spec.nr_in + spec.nr_steps; n++) {
                                if (varMarks[i][k + 1][n]) {
                                    tmpLits[0] = pabc::Abc_Var2Lit(varMarks[i][k][j], 1);
                                    tmpLits[1] = pabc::Abc_Var2Lit(varMarks[i][k + 1][n], 1);
                                    //printf("%d --> !%d\n", varMarks[i][k][j], varMarks[i][k + 1][n]);
                                    if (!solver->add_clause(tmpLits, tmpLits + 2))
                                        assert(false);
                                }
                            }
                        }
                    }
                }
            }
            for (int i = 0; i < spec.nr_in + spec.nr_steps - 1; i++) {
                auto vArray = pabc::Vec_WecEntry(vOutLits, i);
                if (Vec_IntSize(vArray) == 0)
                    continue;
                if (!solver->add_clause(pabc::Vec_IntArray(vArray), pabc::Vec_IntArray(vArray) + pabc::Vec_IntSize(vArray)))
                    assert(false);
            }
        }

        int Maj_ManValue(int iMint, int nVars)
        {
            int k, Count = 0;
            for (k = 0; k < nVars; k++)
                Count += (iMint >> k) & 1;
            return (int)(Count > nVars / 2);
        }

        bool add_cnf(const spec& spec, int iMint)
        {
            // save minterm values
            int Value = Maj_ManValue(iMint, spec.nr_in);
            for (int i = 0; i < spec.nr_in; i++)
                varVals[i] = (iMint >> i) & 1;
            solver->set_nr_vars(iVar + 4 * spec.nr_steps);
            printf( "Adding clauses for minterm %d.\n", iMint );
            for (int i = spec.nr_in; i < spec.nr_in + spec.nr_steps; i++) {
                // fanin connectivity
                const auto iBaseSatVarI = iVar + 4 * (i - spec.nr_in);
                for (int k = 0; k < 3; k++)
                {
                    for (int j = 0; j < spec.nr_in + spec.nr_steps; j++) if (varMarks[i][k][j])
                    {
                        int iBaseSatVarJ = iVar + 4 * (j - spec.nr_in);
                        for (int n = 0; n < 2; n++) {
                            auto nLits = 0;
                            pLits[nLits++] = pabc::Abc_Var2Lit(varMarks[i][k][j], 1);
                            pLits[nLits++] = pabc::Abc_Var2Lit(iBaseSatVarI + k, n);
                            if (j >= spec.nr_in)
                                pLits[nLits++] = pabc::Abc_Var2Lit(iBaseSatVarJ + 3, !n);
                            else if (varVals[j] == n)
                                continue;
                            if (!solver->add_clause(pLits, pLits + nLits))
                                return false;
                        }
                    }
                }
                // node functionality
                for (int n = 0; n < 2; n++) {
                    if (i == (spec.nr_in + spec.nr_steps - 1) && n == Value)
                        continue;
                    for (int k = 0; k < 3; k++) {
                        auto nLits = 0;
                        if (k != 0) pLits[nLits++] = pabc::Abc_Var2Lit(iBaseSatVarI + 0, n);
                        if (k != 1) pLits[nLits++] = pabc::Abc_Var2Lit(iBaseSatVarI + 1, n);
                        if (k != 2) pLits[nLits++] = pabc::Abc_Var2Lit(iBaseSatVarI + 2, n);
                        if (i != (spec.nr_in + spec.nr_steps - 1)) pLits[nLits++] = pabc::Abc_Var2Lit(iBaseSatVarI + 3, !n);
                        assert(nLits <= 3);
                        if (!solver->add_clause(pLits, pLits + nLits))
                            return false;
                    }
                }
            }
            iVar += 4 * (spec.nr_steps);
            return true;
        }
        
        bool cegar_encode(const spec& spec)
        {
            memset(varMarks, 0, sizeof(varMarks));
            for (int i = 0; i < spec.nr_in + spec.nr_steps; i++) {
                sim_tts[i] = kitty::dynamic_truth_table(spec.nr_in);
                if (i < spec.nr_in) {
                    kitty::create_nth_var(sim_tts[i], i);
                }
            }
            add_base_variables(spec);
            add_base_cnf(spec);

            return true;
        }

        bool cegar_encode(const spec& spec, const fence& fence)
        {
            memset(varMarks, 0, sizeof(varMarks));
            for (int i = 0; i < spec.nr_in + spec.nr_steps; i++) {
                sim_tts[i] = kitty::dynamic_truth_table(spec.nr_in);
                if (i < spec.nr_in) {
                    kitty::create_nth_var(sim_tts[i], i);
                }
            }
            update_level_map(spec, fence);
            add_base_variables(spec, fence);
            add_base_cnf(spec);

            return true;
        }
        
        /// Simulates the current state of the encoder and returns the index
        /// of the first minterm which is different from the specified function.
        /// Returns -1 if no such index exists.
        int simulate(const spec& spec)
        {
            kitty::dynamic_truth_table* pFanins[3];

            for (int i = spec.nr_in; i < spec.nr_in + spec.nr_steps; i++) {
                for (int k = 0; k < 3; k++)
                    pFanins[k] = &sim_tts[find_fanin(spec, i, k)];
                sim_tts[i] = kitty::ternary_majority(*pFanins[0], *pFanins[1], *pFanins[2]);
            }
            /*
            const auto iMint =
                kitty::find_first_bit_difference(sim_tts[spec.nr_in + spec.nr_steps - 1], spec[0]);
            */
            int iMint = -1;
            kitty::static_truth_table<6> tt;
            for (int i = 1; i < (1 << spec.nr_in); i++) {
                kitty::create_from_words(tt, &i, &i + 1);
                const auto nOnes = kitty::count_ones(tt);
                if (nOnes < spec.nr_in / 2 || nOnes > ((spec.nr_in/2) + 1)) {
                    continue;
                }
                if (kitty::get_bit(sim_tts[spec.nr_in + spec.nr_steps - 1], i)
                    == kitty::get_bit(spec[0], i)) {
                    continue;
                }
                iMint = i;
                break;
            }

            assert(iMint < (1 << spec.nr_in));
            return iMint;
        }

        int get_nr_vars() const { return iVar; }
        
    };

    inline void ditt_maj_synthesize(int nr_in)
    {
        spec spec;
        bmcg_wrapper solver;
        ditt_maj_encoder encoder(solver);
        kitty::dynamic_truth_table tt(nr_in);
        kitty::create_majority(tt);

        spec[0] = tt;
        spec.preprocess();

        fence f;
        po_filter<unbounded_generator> g(unbounded_generator(spec.initial_steps), 1, 3);
        while (true) {
            g.next_fence(f);
            printf("next fence:\n"); print_fence(f); printf("\n");
            spec.nr_steps = f.nr_nodes();
            solver.restart();
            if (!encoder.cegar_encode(spec, f)) {
                continue;
            }
            auto iMint = 0;
            for (int i = 0; iMint != -1; i++) {
                if (!encoder.add_cnf(spec, iMint)) {
                    break;
                }
                printf("Iter %3d : ", i);
                printf("Var =%5d  ", encoder.get_nr_vars());
                printf("Cla =%6d  ", solver.nr_clauses());
                printf("Conf =%9d\n", solver.nr_conflicts());
                const auto status = solver.solve(0);
                if (status == failure) {
                    printf("The problem has no solution\n");
                    break;
                }
                iMint = encoder.simulate(spec);
            }
            if (iMint == -1) {
                printf("found solution!\n");
                break;
            }
        }
    }
}