

/************************************************************************************[ParseUtils.h]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007-2010, Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

#ifndef MapleCOMSPS_ParseUtils_h
#define MapleCOMSPS_ParseUtils_h

#include <stdlib.h>
#include <stdio.h>

#include <zlib.h>

#include "BMCOracle.hpp"

//-------------------------------------------------------------------------------------------------
// A simple buffered character stream class:

static const int buffer_size = 1048576;

class StreamBuffer
{
    gzFile in;
    unsigned char buf[buffer_size];
    int pos;
    int size;

    void assureLookahead()
    {
        if (pos >= size)
        {
            pos = 0;
            size = gzread(in, buf, sizeof(buf));
        }
    }

public:
    explicit StreamBuffer(gzFile i) : in(i), pos(0), size(0) { assureLookahead(); }

    int operator*() const { return (pos >= size) ? EOF : buf[pos]; }
    void operator++()
    {
        pos++;
        assureLookahead();
    }
    int position() const { return pos; }
};

//-------------------------------------------------------------------------------------------------
// End-of-file detection functions for StreamBuffer and char*:

static inline bool isEof(StreamBuffer &in) { return *in == EOF; }
static inline bool isEof(const char *in) { return *in == '\0'; }

//-------------------------------------------------------------------------------------------------
// Generic parse functions parametrized over the input-stream type.

template <class B>
static void skipWhitespace(B &in)
{
    while ((*in >= 9 && *in <= 13) || *in == 32)
        ++in;
}

template <class B>
static void skipLine(B &in)
{
    for (;;)
    {
        if (isEof(in))
            return;
        if (*in == '\n')
        {
            ++in;
            return;
        }
        ++in;
    }
}

template <class B>
static int parseInt(B &in)
{
    int val = 0;
    bool neg = false;
    skipWhitespace(in);
    if (*in == '-')
        neg = true, ++in;
    else if (*in == '+')
        ++in;
    if (*in < '0' || *in > '9')
        fprintf(stderr, "c PARSE ERROR! Unexpected char: %c\n", *in), exit(3);
    while (*in >= '0' && *in <= '9')
        val = val * 10 + (*in - '0'),
        ++in;
    return neg ? -val : val;
}

// String matching: in case of a match the input iterator will be advanced the corresponding
// number of characters.
template <class B>
static bool match(B &in, const char *str)
{
    int i;
    for (i = 0; str[i] != '\0'; i++)
        if (in[i] != str[i])
            return false;

    in += i;

    return true;
}

// String matching: consumes characters eagerly, but does not require random access iterator.
template <class B>
static bool eagerMatch(B &in, const char *str)
{
    for (; *str != '\0'; ++str, ++in)
        if (*str != *in)
            return false;
    return true;
}

//=================================================================================================

template <class B>
int parseVar(B &in)
{
    int parsed_val = -1, var = -1;
    for (;;)
    {
        skipWhitespace(in);
        if (*in < '0' || *in > '9')
        {
            ++in;
            continue;
        }
        parsed_val = parseInt(in);
        if (parsed_val > 0)
        {
            var = parsed_val;
            break;
        }
    }
    return var;
}

template <class B>
int parseVarLoopOrSize(B &in)
{
    int parsed_val = -1, var = -1;
    for (;;)
    {
        skipWhitespace(in);
        if (*in < '0' || *in > '9')
        {
            ++in;
            continue;
        }
        parsed_val = parseInt(in);
        if (parsed_val >= 0)
        {
            var = parsed_val;
            break;
        }
    }
    return var;
}

template <class B>
std::string parseNameVar(B &in)
{
    std::string name = "";
    for (;;)
    {
        skipWhitespace(in);
        if (eagerMatch(in, "Variable"))
        {
            skipWhitespace(in);
            while (*in != '\n')
            {
                name += *in;
                ++in;
            }
            break;
        }
        else
        {
            ++in;
            continue;
        }
    }
    return name;
}

template <class B>
static bool operator_ltlspec(B &in, bool bis = false)
{
    if (bis)
    {
        return (*in == '(' || *in == ')' || *in == '!' ||
                *in == '&' || *in == '|' || *in == '<' ||
                *in == '>' || *in == ' ' || *in == EOF);
    }
    return (*in == '(' || *in == ')' || *in == '!' || *in == 'G' || *in == 'F' || *in == 'U' ||
            *in == 'X' || *in == 'R' || *in == 'W' || *in == '&' || *in == '|' || *in == '<' ||
            *in == '>' || *in == '-' || *in == ' ' || *in == 'V');
}

template <class B>
static void readLTLSPEC(B &in, BMCOracle &oracle)
{
    std::string ltl = "";
    for (;;)
    {
        if (*in == EOF)
            break;
        if (operator_ltlspec(in))
        {
            ltl += *in;
            ++in;
        }
        else
        {
            ltl += "\"";
            while (!operator_ltlspec(in, true))
            {
                ltl += *in;
                ++in;
            }
            ltl += "\"";
        }
    }
    ltl.pop_back();
    ltl.pop_back();
    ltl.pop_back();
    oracle.ltlspec = ltl;
}

// Main function
static void readHeadFile(BMCOracle *oracle, const char *filename)
{
    gzFile input_stream = gzopen(filename, "rb");
    StreamBuffer in(input_stream);
    std::vector<int> lits;
    int step = -1;
    oracle->max_vars = 0;
    oracle->first_var = INT_MAX;

    // Get information about pure variables
    for (;;)
    {
        if (*in == 'c')
        {
            ++in;
            ++in;
            // c @@@@@@ Time
            if (*in == '@')
            {
                step++;
            }
            // c CNF variable
            else if (*in == 'C')
            {
                int var_time = parseVar(in);
                std::string name = parseNameVar(in);
                varInfo vf;
                vf.num_step = step;
                vf.name = name;
                while ((int)oracle->info_variables.size() <= var_time)
                    oracle->info_variables.push_back(varInfo{});
                oracle->info_variables[var_time] = vf;
                oracle->nb_model_variable++;
                if (var_time < oracle->first_var)
                    oracle->first_var = var_time;
            }
            // c LOOP variable
            else if (*in == 'L')
            {
                int var_time = parseVarLoopOrSize(in);
                int loop_sz = parseVarLoopOrSize(in);
                oracle->rename_lit_loop.emplace_back(var_time); // ajout des loop variables dans l'orddre
                varInfo vf;
                vf.num_step = loop_sz;
                vf.name = "loop" + std::to_string(var_time);
                vf.property = true; // refaire dans init_spot de BMCOracle
                while ((int)oracle->info_variables.size() <= var_time)
                    oracle->info_variables.push_back(varInfo{});
                oracle->info_variables[var_time] = vf;
            }
            // c model
            else if (*in == 'm')
            {
                skipLine(in);
                break;
            }
            skipLine(in);
        }
        else
            ++in;
    }
    oracle->K = step;

    // Do normal parsing to get LTLSPEC at the end
    for (;;)
    {
        skipWhitespace(in);
        if (*in == EOF)
            break;
        else if (*in == 'p')
        {
            if (eagerMatch(in, "p cnf"))
            {
                oracle->max_vars = parseInt(in);
                parseInt(in);
            }
            else
                printf("PARSE ERROR! Unexpected char: %c\n", *in), exit(3);
        }
        else if (*in == 'c')
        {
            ++in;
            ++in;
            // GET information about LTLSPEC
            if (*in == 'L')
            {
                ++in;
                if (*in == 'T' && eagerMatch(in, "TLSPEC "))
                {
                    readLTLSPEC(in, *oracle);
                    break;
                }
            }
            else
                skipLine(in);
        }
        else
            skipLine(in);
    }
}

#endif