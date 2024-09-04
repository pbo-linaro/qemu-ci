/*
 *  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * This program generates the semantics file that is processed by
 * the do_qemu.py script.  We use the C preporcessor to manipulate the
 * files imported from the Hexagon architecture library.
 */

#include <stdio.h>
#define STRINGIZE(X) #X

int main(int argc, char *argv[])
{
    FILE *outfile;

    if (argc != 2) {
        fprintf(stderr, "Usage: gen_semantics ouptputfile\n");
        return 1;
    }
    outfile = fopen(argv[1], "w");
    if (outfile == NULL) {
        fprintf(stderr, "Cannot open %s for writing\n", argv[1]);
        return 1;
    }

/*
 * Process the instruction definitions
 *     Scalar core instructions have the following form
 *         Q6INSN(A2_add,"Rd32=add(Rs32,Rt32)",ATTRIBS(),
 *         "Add 32-bit registers",
 *         { RdV=RsV+RtV;})
 *     HVX instructions have the following form
 *         EXTINSN(V6_vinsertwr, "Vx32.w=vinsert(Rt32)",
 *         ATTRIBS(A_EXTENSION,A_CVI,A_CVI_VX),
 *         "Insert Word Scalar into Vector",
 *         VxV.uw[0] = RtV;)
 */
#define Q6INSN(TAG, BEH, ATTRIBS, DESCR, SEM) \
    do { \
        fprintf(outfile, "SEMANTICS( \\\n" \
                         "    \"%s\", \\\n" \
                         "    %s, \\\n" \
                         "    \"\"\"%s\"\"\" \\\n" \
                         ")\n", \
                #TAG, STRINGIZE(BEH), STRINGIZE(SEM)); \
        fprintf(outfile, "ATTRIBUTES( \\\n" \
                         "    \"%s\", \\\n" \
                         "    \"%s\" \\\n" \
                         ")\n", \
                #TAG, STRINGIZE(ATTRIBS)); \
    } while (0);
#define EXTINSN(TAG, BEH, ATTRIBS, DESCR, SEM) \
    do { \
        fprintf(outfile, "SEMANTICS( \\\n" \
                         "    \"%s\", \\\n" \
                         "    %s, \\\n" \
                         "    \"\"\"%s\"\"\" \\\n" \
                         ")\n", \
                #TAG, STRINGIZE(BEH), STRINGIZE(SEM)); \
        fprintf(outfile, "ATTRIBUTES( \\\n" \
                         "    \"%s\", \\\n" \
                         "    \"%s\" \\\n" \
                         ")\n", \
                #TAG, STRINGIZE(ATTRIBS)); \
    } while (0);
#include "imported/allidefs.def"
#undef Q6INSN
#undef EXTINSN

/*
 * Process the macro definitions
 *     Macros definitions have the following form
 *         DEF_MACRO(
 *             fLSBNEW0,
 *             predlog_read(thread,0),
 *             ()
 *         )
 * The important part here is the attributes.  Whenever an instruction
 * invokes a macro, we add the macro's attributes to the instruction.
 */
#define DEF_MACRO(MNAME, BEH, ATTRS) \
    fprintf(outfile, "MACROATTRIB( \\\n" \
                     "    \"%s\", \\\n" \
                     "    \"\"\"%s\"\"\", \\\n" \
                     "    \"%s\" \\\n" \
                     ")\n", \
            #MNAME, STRINGIZE(BEH), STRINGIZE(ATTRS));
#include "imported/macros.def"
#undef DEF_MACRO

/*
 * Process the macros for HVX
 */
#define DEF_MACRO(MNAME, BEH, ATTRS) \
    fprintf(outfile, "MACROATTRIB( \\\n" \
                     "    \"%s\", \\\n" \
                     "    \"\"\"%s\"\"\", \\\n" \
                     "    \"%s\" \\\n" \
                     ")\n", \
            #MNAME, STRINGIZE(BEH), STRINGIZE(ATTRS));
#include "imported/allext_macros.def"
#undef DEF_MACRO

    fclose(outfile);
    return 0;
}
