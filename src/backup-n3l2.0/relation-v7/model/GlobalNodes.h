#ifndef SRC_GlobalNodes_H_
#define SRC_GlobalNodes_H_

#include "ModelParams.h"

struct GlobalNodes {
    //sequential LSTM
    vector<vector<LookupNode> > char_inputs;
    vector<WindowBuilder> char_windows;
    vector<vector<UniNode> > char_convs;
    vector<MaxPoolNode> char_represents;

    vector<LookupNode> word_inputs;
    vector<LookupNode> word_ext_inputs;
    vector<LookupNode> tag_inputs;

    vector<ConcatNode> word_represents;
    vector<UniNode> word_tanh_conv1;

    LSTM1Builder word_left_lstm;
    LSTM1Builder word_right_lstm;

    vector<BiNode> word_tanh_conv2;


    vector<LookupNode> label_inputs;
    vector<ConcatNode> tree_input_node;

    //dependency LSTM
    TreeLSTM1Builder dep_b2t_lstm;
    TreeLSTM1Builder dep_t2b_lstm;

    vector<vector<int> > common_roots;



  public:
    inline void resize(int max_length, int max_clength) {
        resizeVec(char_inputs, max_length, max_clength);
        char_windows.resize(max_length);
        resizeVec(char_convs, max_length, max_clength);
        char_represents.resize(max_length);
        for (int idx = 0; idx < max_length; idx++) {
            char_windows[idx].resize(max_clength);
            char_represents[idx].setParam(max_clength);
        }

        word_inputs.resize(max_length);
        word_ext_inputs.resize(max_length);
        tag_inputs.resize(max_length);
        word_represents.resize(max_length);
        word_tanh_conv1.resize(max_length);
        word_left_lstm.resize(max_length);
        word_right_lstm.resize(max_length);
        word_tanh_conv2.resize(max_length);

        label_inputs.resize(max_length);
        tree_input_node.resize(max_length);
        dep_b2t_lstm.resize(max_length);
        dep_t2b_lstm.resize(max_length);

        common_roots.resize(max_length);
        for (int idx = 0; idx < max_length; idx++) {
            common_roots[idx].resize(max_length - idx);
        }
    }

  public:
    inline void initial(ModelParams &params, HyperParams &hyparams, AlignedMemoryPool *mem) {
        int length = word_inputs.size();
        for (int idx = 0; idx < length; idx++) {
            word_inputs[idx].setParam(&params.word_table);
            word_ext_inputs[idx].setParam(&params.word_ext_table);
            tag_inputs[idx].setParam(&params.tag_table);
            label_inputs[idx].setParam(&params.label_table);
            word_tanh_conv1[idx].setParam(&params.word_tanh_conv1); //TODO:
            word_tanh_conv2[idx].setParam(&params.word_tanh_conv2);
            for (int idy = 0; idy < char_inputs[idx].size(); idy++) {
                char_inputs[idx][idy].setParam(&params.char_table);
                char_convs[idx][idy].setParam(&params.char_tanh_conv);
            }
        }

        word_left_lstm.init(&params.word_left_lstm, hyparams.dropProb, true, mem);
        word_right_lstm.init(&params.word_right_lstm, hyparams.dropProb, false, mem);

        dep_b2t_lstm.init(&params.dep_b2t_lstm, hyparams.dropProb, true, mem);
        dep_t2b_lstm.init(&params.dep_t2b_lstm, hyparams.dropProb, false, mem);


        for (int idx = 0; idx < length; idx++) {
            for (int idy = 0; idy < char_inputs[idx].size(); idy++) {
                char_inputs[idx][idy].init(hyparams.char_dim, hyparams.dropProb, mem);
                char_windows[idx].init(hyparams.char_dim, hyparams.char_context, mem);
                char_convs[idx][idy].init(hyparams.char_hidden_dim, hyparams.dropProb, mem);
            }
            char_represents[idx].init(hyparams.char_hidden_dim, hyparams.dropProb, mem);
            word_inputs[idx].init(hyparams.word_dim, hyparams.dropProb, mem);
            word_ext_inputs[idx].init(hyparams.word_ext_dim, hyparams.dropProb, mem);
            tag_inputs[idx].init(hyparams.tag_dim, hyparams.dropProb, mem);
            word_represents[idx].init(hyparams.word_represent_dim, -1, mem);
            word_tanh_conv1[idx].init(hyparams.word_hidden1_dim, hyparams.dropProb, mem);
            word_tanh_conv2[idx].init(hyparams.word_hidden2_dim, hyparams.dropProb, mem);
            label_inputs[idx].init(hyparams.label_dim, hyparams.dropProb, mem);
            tree_input_node[idx].init(hyparams.tree_input_dim, -1, mem);
        }
    }


  public:
    inline void forward(Graph* cg, const Instance& inst, HyperParams* hyparams) {
        int word_size = inst.words.size();
        string currWord, currPos;
        for (int idx = 0; idx < word_size; idx++) {
            currWord = normalize_to_lower(inst.words[idx]);
            currPos = inst.tags[idx];

            int char_size = inst.chars[idx].size();
            if (char_size > char_inputs[idx].size()) {
                char_size = char_inputs[idx].size();
            }
            for (int idy = 0; idy < char_size; idy++) {
                char_inputs[idx][idy].forward(cg, inst.chars[idx][idy]);
            }
            char_windows[idx].forward(cg, getPNodes(char_inputs[idx], char_size));
            for (int idy = 0; idy < char_size; idy++) {
                char_convs[idx][idy].forward(cg, &(char_windows[idx]._outputs[idy]));
            }
            char_represents[idx].forward(cg, getPNodes(char_convs[idx], char_size));

            word_ext_inputs[idx].forward(cg, currWord);


            // Unknown word strategy: STOCHASTIC REPLACEMENT

            int c = hyparams->word_stat[currWord];
            dtype rand_drop = rand() / double(RAND_MAX);
            if (cg->train && c <= 1 && rand_drop < 0.5) {
                currWord = unknownkey;
            }


            word_inputs[idx].forward(cg, currWord);
            tag_inputs[idx].forward(cg, currPos);
            word_represents[idx].forward(cg, &word_inputs[idx], &word_ext_inputs[idx], &tag_inputs[idx], &(char_represents[idx]));
        }

        for (int idx = 0; idx < word_size; idx++) {
            word_tanh_conv1[idx].forward(cg, &(word_represents[idx]));
        }
        word_left_lstm.forward(cg, getPNodes(word_tanh_conv1, word_size));
        word_right_lstm.forward(cg, getPNodes(word_tanh_conv1, word_size));

        //FIXME:
        for (int idx = 0; idx < word_size; idx++) {
            word_tanh_conv2[idx].forward(cg, &(word_left_lstm._hiddens[idx]), &(word_right_lstm._hiddens[idx]));
        }

        for (int idx = 0; idx < word_size; idx++) {
            label_inputs[idx].forward(cg, inst.labels[idx]);
            tree_input_node[idx].forward(cg, &(word_tanh_conv1[idx]), &(label_inputs[idx]));
        }

        dep_b2t_lstm.forward(cg, getPNodes(tree_input_node, word_size), inst.heads, inst.labels);
        dep_t2b_lstm.forward(cg, getPNodes(tree_input_node, word_size), inst.heads, inst.labels);

        vector<vector<int> > paths;
        paths.resize(word_size);
        for (int idx = 0; idx < word_size; idx++) {
            paths[idx].push_back(idx);
            int curHead = inst.heads[idx];
            while (curHead != -1) {
                paths[idx].insert(paths[idx].begin(), curHead);
                curHead = inst.heads[curHead];
            }
        }

        for (int idx = 0; idx < word_size; idx++) {
            common_roots[idx][0] = idx;
            for (int dist = 1; dist < word_size - idx; dist++) {
                int idy = idx + dist;
                int mincount = paths[idx].size() > paths[idy].size() ? paths[idy].size() : paths[idx].size();
                int count = 0;
                for (; count < mincount; count++) {
                    if (paths[idx][count] != paths[idy][count]) {
                        common_roots[idx][dist] = paths[idx][count - 1];
                        break;
                    }
                }

                if (count == mincount) {
                    common_roots[idx][dist] = paths[idx][count - 1];
                }
            }
        }


    }

};

#endif /* SRC_GlobalNodes_H_ */
