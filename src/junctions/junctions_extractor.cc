/*  junctions_extractor.h -- Declarations for `junctions extract` command

    Copyright (c) 2015, The Griffith Lab

    Author: Avinash Ramu <aramu@genome.wustl.edu>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.  */

#include <getopt.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include "common.h"
#include "junctions_extractor.h"
#include "htslib/sam.h"
#include "htslib/hts.h"
#include "htslib/faidx.h"
#include "htslib/kstring.h"

using namespace std;

//Parse the options passed to this tool
int JunctionsExtractor::parse_options(int argc, char *argv[]) {
    optind = 1; //Reset before parsing again.
    int c;
    stringstream help_ss;
    while((c = getopt(argc, argv, "ha:i:I:o:r:")) != -1) {
        switch(c) {
            case 'a':
                min_anchor_length_ = atoi(optarg);
                break;
            case 'i':
                min_intron_length_ = atoi(optarg);
                break;
            case 'I':
                max_intron_length_ = atoi(optarg);
                break;
            case 'o':
                output_file_ = string(optarg);
                break;
            case 'r':
                region_ = string(optarg);
                break;
            case 'h':
                usage(help_ss);
                throw common::cmdline_help_exception(help_ss.str());
            case '?':
            default:
                throw runtime_error("Error parsing inputs!");
        }
    }
    if(argc - optind >= 1) {
        bam_ = string(argv[optind++]);
    }
    if(optind < argc || bam_ == "NA") {
        throw runtime_error("\nError parsing inputs!");
    }
    cerr << endl << "Minimum junction anchor length: " << min_anchor_length_;
    cerr << endl << "Minimum intron length: " << min_intron_length_;
    cerr << endl << "Maximum intron length: " << max_intron_length_;
    cerr << endl << "Alignment: " << bam_;
    cerr << endl << "Output file: " << output_file_;
    cerr << endl;
    return 0;
}

//Usage statement for this tool
int JunctionsExtractor::usage(ostream& out) {
    out << "\nUsage:\t\t" << "regtools junctions extract [options] indexed_alignments.bam";
    out << "\nOptions:";
    out << "\t" << "-a INT\tMinimum anchor length. Junctions which satisfy a minimum "
                     "anchor length on both sides are reported. [8]";
    out << "\n\t\t" << "-i INT\tMinimum intron length. [70]";
    out << "\n\t\t" << "-I INT\tMaximum intron length. [500000]";
    out << "\n\t\t" << "-o FILE\tThe file to write output to. [STDOUT]";
    out << "\n\t\t" << "-r STR\tThe region to identify junctions "
                     "in \"chr:start-end\" format. Entire BAM by default.";
    out << "\n";
    return 0;
}

//Get the BAM filename
string JunctionsExtractor::get_bam() {
    return bam_;
}

//Name the junction based on the number of junctions
// in the map.
string JunctionsExtractor::get_new_junction_name() {
    int index = junctions_.size() + 1;
    stringstream name_ss;
    name_ss << "JUNC" << setfill('0') << setw(8) << index;
    return name_ss.str();
}

//Do some basic qc on the junction
bool JunctionsExtractor::junction_qc(Junction &j1) {
    if(j1.end - j1.start < min_intron_length_ ||
       j1.end - j1.start > max_intron_length_) {
        return false;
    }
    if(j1.start - j1.thick_start >= min_anchor_length_)
        j1.has_left_min_anchor = true;
    if(j1.thick_end - j1.end >= min_anchor_length_)
        j1.has_right_min_anchor = true;
    return true;
}

//Add a junction to the junctions map
//The read_count field is the number of reads supporting the junction.
int JunctionsExtractor::add_junction(Junction j1) {
    //Check junction_qc
    if(!junction_qc(j1)) {
        return 0;
    }

    //Construct key chr:start-end:strand
    stringstream s1;
    string start, end;
    s1 << j1.start; start = s1.str();
    s1 << j1.end; end = s1.str();
    string key = j1.chrom + string(":") + start + "-" + end + ":" + j1.strand;

    //Check if new junction
    if(!junctions_.count(key)) {
        j1.name = get_new_junction_name();
        j1.read_count = 1;
        j1.score = common::num_to_str(j1.read_count);
    } else { //existing junction
        Junction j0 = junctions_[key];
        //increment read count
        j1.read_count = j0.read_count + 1;
        j1.score = common::num_to_str(j1.read_count);
        //Keep the same name
        j1.name = j0.name;
        //Check if thick starts are any better
        if(j0.thick_start < j1.thick_start)
            j1.thick_start = j0.thick_start;
        if(j0.thick_end > j1.thick_end)
            j1.thick_end = j0.thick_end;
        //preserve min anchor information
        j1.has_left_min_anchor = j1.has_left_min_anchor || j0.has_left_min_anchor;
        j1.has_right_min_anchor = j1.has_right_min_anchor || j0.has_right_min_anchor;
    }
    //Add junction and check anchor while printing.
    junctions_[key] = j1;
    return 0;
}

//Print all the junctions - this function needs work
vector<Junction> JunctionsExtractor::get_all_junctions() {
    //Sort junctions by position
    if(!junctions_sorted_) {
        create_junctions_vector();
        sort_junctions(junctions_vector_);
        junctions_sorted_ = true;
    }
    return junctions_vector_;
}

//Print all the junctions - this function needs work
void JunctionsExtractor::print_all_junctions(ostream& out) {
    ofstream fout;
    if(output_file_ != string("NA")) {
        fout.open(output_file_.c_str());
    }
    //Sort junctions by position
    if(!junctions_sorted_) {
        create_junctions_vector();
        sort_junctions(junctions_vector_);
        junctions_sorted_ = true;
    }
    for(vector<Junction> :: iterator it = junctions_vector_.begin();
        it != junctions_vector_.end(); it++) {
        Junction j1 = *it;
        if(j1.has_left_min_anchor && j1.has_right_min_anchor) {
            if(fout.is_open())
                j1.print(fout);
            else
                j1.print(out);
        }
    }
    if(fout.is_open())
        fout.close();
}

//Get the strand from the XS aux tag
void JunctionsExtractor::set_junction_strand(bam1_t *aln, Junction& j1) {
    uint8_t *p = bam_aux_get(aln, "XS");
    if(p != NULL) {
        char strand = bam_aux2A(p);
        strand ? j1.strand = string(1, strand) : j1.strand = string(1, '?');
    } else {
        j1.strand = string(1, '?');
        return;
    }
}

//Parse junctions from the read and store in junction map
int JunctionsExtractor::parse_alignment_into_junctions(bam_hdr_t *header, bam1_t *aln) {
    int n_cigar = aln->core.n_cigar;
    if (n_cigar <= 1) // max one cigar operation exists(likely all matches)
        return 0;

    int chr_id = aln->core.tid;
    int read_pos = aln->core.pos;
    string chr(header->target_name[chr_id]);
    uint32_t *cigar = bam_get_cigar(aln);

    /*
    //Skip duplicates
    int flag = aln->core.flag;
    if(flag & 1024) {
        cerr << "Skipping read_pos " << read_pos << " flag " << flag << endl;
        return 0;
    }
    */

    Junction j1;
    j1.chrom = chr;
    j1.start = read_pos; //maintain start pos of junction
    j1.thick_start = read_pos;
    set_junction_strand(aln, j1);
    bool started_junction = false;
    for (int i = 0; i < n_cigar; ++i) {
        char op =
               bam_cigar_opchr(cigar[i]);
        int len =
               bam_cigar_oplen(cigar[i]);
        switch(op) {
            case 'N':
                if(!started_junction) {
                    j1.end = j1.start + len;
                    j1.thick_end = j1.end;
                    //Start the first one and remains started
                    started_junction = true;
                } else {
                    //Add the previous junction
                    try {
                        add_junction(j1);
                    } catch (const std::logic_error& e) {
                        cout << e.what() << '\n';
                    }
                    j1.thick_start = j1.end;
                    j1.start = j1.thick_end;
                    j1.end = j1.start + len;
                    j1.thick_end = j1.end;
                    //For clarity - the next junction is now open
                    started_junction = true;
                }
                break;
            case '=':
            case 'M':
                if(!started_junction)
                    j1.start += len;
                else
                    j1.thick_end += len;
                break;
            //No mismatches allowed in anchor
            case 'D':
            case 'X':
                if(!started_junction) {
                    j1.start += len;
                    j1.thick_start = j1.start;
                } else {
                    try {
                        add_junction(j1);
                    } catch (const std::logic_error& e) {
                        cout << e.what() << '\n';
                    }
                    //Don't include these in the next anchor
                    j1.start = j1.thick_end + len;
                    j1.thick_start = j1.start;
                }
                started_junction = false;
                break;
            case 'I':
            case 'S':
                if(!started_junction)
                    j1.thick_start = j1.start;
                else {
                    try {
                        add_junction(j1);
                    } catch (const std::logic_error& e) {
                        cout << e.what() << '\n';
                    }
                    //Don't include these in the next anchor
                    j1.start = j1.thick_end;
                    j1.thick_start = j1.start;
                }
                started_junction = false;
                break;
            case 'H':
                break;
            default:
                cerr << "Unknown cigar " << op;
                break;
        }
    }
    if(started_junction) {
        try {
            add_junction(j1);
        } catch (const std::logic_error& e) {
            cout << e.what() << '\n';
        }
    }
    return 0;
}

//The workhorse - identifies junctions from BAM
int JunctionsExtractor::identify_junctions_from_BAM() {
    if(!bam_.empty()) {
        //open BAM for reading
        samFile *in = sam_open(bam_.c_str(), "r");
        if(in == NULL) {
            throw runtime_error("Unable to open BAM/SAM file.");
        }
        //Load the index
        hts_idx_t *idx = sam_index_load(in, bam_.c_str());
        if(idx == NULL) {
            throw runtime_error("Unable to open BAM/SAM index."
                                " Make sure alignments are indexed");
        }
        //Get the header
        bam_hdr_t *header = sam_hdr_read(in);
        //Initialize iterator
        hts_itr_t *iter = NULL;
        //Move the iterator to the region we are interested in
        iter  = sam_itr_querys(idx, header, region_.c_str());
        if(header == NULL || iter == NULL) {
            sam_close(in);
            throw runtime_error("Unable to iterate to region within BAM.");
        }
        //Initiate the alignment record
        bam1_t *aln = bam_init1();
        while(sam_itr_next(in, iter, aln) >= 0) {
            parse_alignment_into_junctions(header, aln);
        }
        hts_itr_destroy(iter);
        hts_idx_destroy(idx);
        bam_destroy1(aln);
        bam_hdr_destroy(header);
        sam_close(in);
    }
    return 0;
}

//Create the junctions vector from the map
void JunctionsExtractor::create_junctions_vector() {
    for(map<string, Junction> :: iterator it = junctions_.begin();
        it != junctions_.end(); it++) {
        Junction j1 = it->second;
        junctions_vector_.push_back(j1);
    }
}
