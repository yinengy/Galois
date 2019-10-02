#ifndef EDGE_MINER_H_
#define EDGE_MINER_H_
#include "miner.h"
#include "egonet.h"
#include "dfscode.h"
#include "edgelist.h"
#include "embedding_list.h"
#include "domain_support.h"

//typedef galois::InsertBag<DFS> InitPatternQueue;
typedef galois::gstl::Vector<DFS> InitPatternQueue;
typedef std::vector<Edge> InitEmbeddingList;
typedef std::map<InitPattern, InitEmbeddingList> InitEmbeddingLists;

struct Status {
	//int thread_id;
	//unsigned task_split_level;
	//unsigned embeddings_regeneration_level;
	unsigned current_dfs_level;
	int frequent_patterns_count;
	//bool is_running;
	DFSCode DFS_CODE;
	DFSCode DFS_CODE_IS_MIN;
	CGraph GRAPH_IS_MIN;
	std::vector<std::deque<DFS> > dfs_task_queue;
	//std::deque<DFSCode> dfscodes_to_process;
};
typedef galois::substrate::PerThreadStorage<Status> MtStatus; // Multi-threaded Status

class EdgeMiner : public Miner {
public:
	EdgeMiner(Graph *g, unsigned size) {
		graph = g;
		threshold = minsup;
		max_size = size;
		//for (int i = 0; i < numThreads; i++)
		//	emb_lists.getLocal(i)->allocate(0, size);
		edge_list.init(*g, true);
		total_num.reset();
	}
	virtual ~EdgeMiner() {}
	virtual bool toExtend(unsigned n, const EdgeEmbedding &emb, unsigned pos) {
		//return emb.get_key(pos) == 0;
		return true;
	}
	virtual bool toAdd(unsigned n, const EdgeEmbedding &emb, VertexId src, VertexId dst, unsigned pos, BYTE &existed, VertexSet vertex_set) {
		//return !is_edge_automorphism(n, emb, pos, src, dst, existed, vertex_set);
		return true;
	}
	virtual unsigned getPattern(unsigned n, unsigned i, VertexId dst, const EdgeEmbedding &emb, unsigned pos) {
		return 0;
	}
	virtual void reduction(unsigned pid) {
		//total_num += 1;
	}
	virtual void print_output() { }

	void init_dfs() {
		int single_edge_patterns = 0;
		int num_embeddings = 0;
		#ifdef PRECOMPUTE
		galois::do_all(galois::iterate((size_t)0, edge_list.size()),
			[&](const size_t& pos) {
				InitMap *lmap = init_pattern_maps.getLocal();
				Edge edge = edge_list.get_edge(pos);
				auto& src_label = graph->getData(edge.src);
				auto& dst_label = graph->getData(edge.dst);
				if (src_label <= dst_label) {
					InitPattern key = get_init_pattern(src_label, dst_label);
					if (lmap->find(key) == lmap->end()) {
						(*lmap)[key] = new DomainSupport(2);
						(*lmap)[key]->set_threshold(threshold);
					}
					(*lmap)[key]->add_vertex(0, edge.src);
					(*lmap)[key]->add_vertex(1, edge.dst);
				}
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(), galois::no_conflicts(),
			galois::loopname("InitReduction")
		);
		init_map = *(init_pattern_maps.getLocal(0));
		for (auto i = 1; i < numThreads; i++) {
			for (auto element : *init_pattern_maps.getLocal(i)) {
				DomainSupport *support = element.second;
				if (init_map.find(element.first) == init_map.end()) {
					init_map[element.first] = support;
				} else {
					for (unsigned i = 0; i < 2; i ++) {
						if (!init_map[element.first]->has_domain_reached_support(i)) {
							if (support->has_domain_reached_support(i))
								init_map[element.first]->set_domain_frequent(i);
							else init_map[element.first]->add_vertices(i, support->domain_sets[i]);
						}
					}
				}
			}
		}
		single_edge_patterns = init_map.size();

		int num_freq_embeddings = 0;
		// classify all the single-edge embeddings
		//for (auto edge : edge_list) {
		for (size_t eid = 0; eid < edge_list.size(); eid ++) {
			Edge edge = edge_list.get_edge(eid);
			auto& src_label = graph->getData(edge.src);
			auto& dst_label = graph->getData(edge.dst);
			if (src_label <= dst_label) {
				num_embeddings ++;
				InitPattern key = get_init_pattern(src_label, dst_label);
				if (init_map[key]->get_support()) {
					num_freq_embeddings ++;
					//init_emb_lists[src_label][dst_label].push(2, &edge, 0); // TODO: do not consider edge label for now
					init_emb_lists[src_label][dst_label].push(2, edge_list.get_edge_ptr(eid), 0);
				}
			}
		}
		int num_freq_patterns = 0;
		// check each pattern-support pair, if the pattern is frequent, put it into the worklist
		for (auto ps_pair : init_map) {
			// if the pattern is frequent, add it to the pattern queue
			if (ps_pair.second->get_support())  {
				auto src_label = ps_pair.first.first;
				auto dst_label = ps_pair.first.second;
				DFS dfs(0, 1, src_label, 0, dst_label);
				init_freq_pattern_queue.push_back(dfs);
				num_freq_patterns ++;
			}
		}
		std::cout << "Number of frequent single-edge patterns: " << num_freq_patterns << "\n";
		std::cout << "Number of frequent single-edge embeddings: " << num_freq_embeddings << "\n";
		#else
		for (size_t eid = 0; eid < edge_list.size(); eid ++) {
			Edge edge = edge_list.get_edge(eid);
			auto& src_label = graph->getData(edge.src);
			auto& dst_label = graph->getData(edge.dst);
			if (src_label <= dst_label) {
				if (init_emb_lists.count(src_label) == 0 || init_emb_lists[src_label].count(dst_label) == 0)
					single_edge_patterns++;
				//if (debug) std::cout << edge.to_string() << " src_label: " << src_label << " dst_label: " << dst_label << "\n";
				init_emb_lists[src_label][dst_label].push(2, edge_list.get_edge_ptr(eid), 0); // single-edge embedding: (num_vertices, edge, pointer_to_parent_embedding)
				num_embeddings ++;
			}
		}
		// for each single-edge pattern, generate a DFS code and push it into the task queue
		for(auto fromlabel = init_emb_lists.begin(); fromlabel != init_emb_lists.end(); ++fromlabel) {
			for(auto tolabel = fromlabel->second.begin(); tolabel != fromlabel->second.end(); ++tolabel) {
				DFS dfs(0, 1, fromlabel->first, 0, tolabel->first);
				init_pattern_queue.push_back(dfs);
			} // for tolabel
		} // for fromlabel
		#endif
		std::cout << "Number of single-edge patterns: " << single_edge_patterns << "\n";
		if (show) std::cout << "num_embeddings = " << num_embeddings << std::endl; 
	}
	void process() {
		init_dfs();
		if (show) std::cout << "Init done!\n";
		#ifdef PRECOMPUTE
		galois::do_all(galois::iterate(init_freq_pattern_queue),
		#else
		galois::do_all(galois::iterate(init_pattern_queue),
		#endif
			[&](const DFS& dfs) {
				Status *local_status = mt_status.getLocal();
				local_status->current_dfs_level = 0;
				std::deque<DFS> tmp;
				local_status->dfs_task_queue.push_back(tmp);
				local_status->dfs_task_queue[0].push_back(dfs);
				local_status->DFS_CODE.push(0, 1, dfs.fromlabel, dfs.elabel, dfs.tolabel);
				dfs_extend_naive(1, init_emb_lists[dfs.fromlabel][dfs.tolabel], *local_status);
				local_status->DFS_CODE.pop();
			},
			galois::chunk_size<CHUNK_SIZE>(), galois::steal(), galois::no_conflicts(),
			galois::loopname("ParallelDfsFSM")
		);
	}
	void dfs_extend_naive(unsigned level, BaseEdgeEmbeddingList &emb_list, Status &status) {
		//if (debug) std::cout << "\nDFSCode: " << status.DFS_CODE << "\n";
		unsigned sup = support(emb_list, status);
		//if (debug) std::cout << "level: " << level << "  support: " << sup << "\n";
		if (sup < threshold) return;
		//if (debug) {
		//	for(size_t i = 0; i < emb_list.size(); i++)
		//	std::cout << "EmbeddingList (num_vertices=" << emb_list[i].num_vertices << "): " << emb_list[i].to_string_all() << "\n";
		//}
		// check if this pattern is canonical: minimal DFSCode
		if (is_min(status) == false) {
			//if (debug) std::cout << "Not minimal dfscode: pruned\n";
			return;
		}
		total_num += 1;
		// list frequent patterns here!!!
		if (debug) {
			std::cout << status.DFS_CODE.to_string(false) << ": sup = " << sup;
			std::cout << ", num_embeddings = " << emb_list.size() << "\n";
			//for (auto it = emb_list.begin(); it != emb_list.end(); it++) std::cout << "\t" << it->to_string_all() << std::endl;
		}
		if (level == max_size) return;
		//unsigned n = level + 1;
		const RMPath &rmpath = status.DFS_CODE.buildRMPath(); // build the right-most path of this pattern
		auto minlabel = status.DFS_CODE[0].fromlabel; 
		auto maxtoc = status.DFS_CODE[rmpath[0]].to; // right-most vertex
		//if (debug) std::cout << "minlabel: " << minlabel << "  maxtoc: " << maxtoc << "\n";
		status.current_dfs_level = level;
		EmbeddingLists2D emb_lists_fwd;
		EmbeddingLists1D emb_lists_bck;
		for (size_t emb_id = 0; emb_id < emb_list.size(); ++ emb_id) {
			//EdgeEmbedding emb(n);
			//emb_list.get_embedding<EdgeEmbedding>(level, emb_id, emb);
			//for (unsigned element_id = 0; element_id < n; ++ element_id) {
			//	VertexId src = emb.get_vertex(element_id);
			//	if (!toExtend(n, emb, element_id)) continue;
			//	for (auto e : graph->edges(src)) {
			//		GNode dst = graph->getEdgeDst(e);
			//		if (toAdd(n, emb, src, dst, element_id, existed, vert_set)) {
			//			auto dst_label = 0, edge_label = 0;
			//			dst_label = graph->getData(dst);
			//		}
			//	}
			//}
			BaseEdgeEmbedding *cur = &emb_list[emb_id];
			unsigned emb_size = cur->num_vertices;
			History history(cur);
			auto e2 = history[rmpath[0]];
			// backward extension
			for (size_t i = rmpath.size() - 1; i >= 1; --i) {
				auto e1 = history[rmpath[i]];
				if (e1 == e2) continue;
				auto src = e2->dst;
				//std::cout << "[backward] emb = " << cur->to_string_all() << ", src = " << src << ", i = " << i << ", e1 = " << e1->to_string() << ", e2 = " << e2->to_string() << ", history = " << history.to_string() << "\n";
				for (auto e : graph->edges(src)) {
					auto dst = graph->getEdgeDst(e);
					//std::cout << "\t dst = " << dst << "\n";
					if (history.hasEdge(src, dst)) continue;
					if (dst == e1->src && graph->getData(e1->dst) <= graph->getData(src)) {
						emb_lists_bck[status.DFS_CODE[rmpath[i]].from].push(emb_size, edge_list.get_edge_ptr(*e), cur);
						break;
					}
				}
			}
			// pure forward extension
			for (auto e : graph->edges(e2->dst)) {
				auto dst = graph->getEdgeDst(e);
				auto& dst_label = graph->getData(dst);
				if (minlabel > dst_label || history.hasVertex(dst)) continue;
				emb_lists_fwd[maxtoc][graph->getData(edge_list.get_edge(*e).dst)].push(emb_size+1, edge_list.get_edge_ptr(*e), cur);
			}
			// backtracked forward extension
			for (size_t i = 0; i < rmpath.size(); ++i) {
				auto e1 = history[rmpath[i]];
				auto tolabel = graph->getData(e1->dst);
				for (auto e : graph->edges(e1->src)) {
					auto dst = graph->getEdgeDst(e);
					//auto elabel = graph->getEdgeData(e);
					auto& dst_label = graph->getData(dst);
					if (e1->dst == dst || minlabel > dst_label || history.hasVertex(dst)) continue;
					if (tolabel <= dst_label) {
						emb_lists_fwd[status.DFS_CODE[rmpath[i]].from][graph->getData(edge_list.get_edge(*e).dst)].push(emb_size+1, edge_list.get_edge_ptr(*e), cur);
					}
				}
			}
		}
		std::deque<DFS> tmp;
		if(status.dfs_task_queue.size() <= level) {
			status.dfs_task_queue.push_back(tmp);
		}
		// insert all extended patterns into the worklist
		// backward
		for (auto to = emb_lists_bck.begin(); to != emb_lists_bck.end(); ++to) {
			DFS dfs(maxtoc, to->first, (LabelT)-1, 0, (LabelT)-1);
			status.dfs_task_queue[level].push_back(dfs);
		}
		// forward
		for (auto from = emb_lists_fwd.rbegin(); from != emb_lists_fwd.rend(); ++from) {
			for (auto tolabel = from->second.begin(); tolabel != from->second.end(); ++tolabel) {
				DFS dfs(from->first, maxtoc + 1, (LabelT)-1, 0, tolabel->first);
				status.dfs_task_queue[level].push_back(dfs);
			}
		}
		while(status.dfs_task_queue[level].size() > 0) {
			DFS dfs = status.dfs_task_queue[level].front(); // take a pattern from the task queue
			status.dfs_task_queue[level].pop_front();
			status.current_dfs_level = level; // ready to go to the next level
			status.DFS_CODE.push(dfs.from, dfs.to, dfs.fromlabel, dfs.elabel, dfs.tolabel); // update the pattern
			if (dfs.is_backward())
				dfs_extend_naive(level+1, emb_lists_bck[dfs.to], status);
			else
				dfs_extend_naive(level+1, emb_lists_fwd[dfs.from][dfs.tolabel], status);
			status.DFS_CODE.pop();
		}
	}
	Ulong get_total_count() { return total_num.reduce(); }

protected:
	UlongAccu total_num;
	unsigned threshold;
	InitMap init_map;
	InitMaps init_pattern_maps; // initial pattern map, only used once, no need to clear
	InitPatternQueue init_pattern_queue;
	InitPatternQueue init_freq_pattern_queue;
	EdgeList edge_list;
	EmbeddingLists2D init_emb_lists;
	MtStatus mt_status;

	bool is_min(Status &status) {
		if (status.DFS_CODE.size() == 1) return true;
		#ifdef USE_CUSTOM
		if (status.DFS_CODE.size() == 2) {
			if (status.DFS_CODE[1].from == 1) {
				if (status.DFS_CODE[0].fromlabel <= status.DFS_CODE[1].tolabel) return true;
			} else {
				assert(status.DFS_CODE[1].from == 0);
				if (status.DFS_CODE[0].fromlabel == status.DFS_CODE[0].tolabel) return false;
				if (status.DFS_CODE[0].tolabel == status.DFS_CODE[1].tolabel && status.DFS_CODE[0].fromlabel < status.DFS_CODE[1].tolabel) return true;
				if (status.DFS_CODE[0].tolabel <  status.DFS_CODE[1].tolabel) return true;
			}
			return false;
		}
		#endif
		status.DFS_CODE.toGraph(status.GRAPH_IS_MIN);
		//if (debug) std::cout << status.GRAPH_IS_MIN.to_string();
		status.DFS_CODE_IS_MIN.clear();
		EmbeddingLists2D emb_lists;
		for (size_t vid = 0; vid < status.GRAPH_IS_MIN.size(); ++ vid) {
			auto vlabel = status.GRAPH_IS_MIN[vid].label;
			for (auto e = status.GRAPH_IS_MIN[vid].edge.begin(); e != status.GRAPH_IS_MIN[vid].edge.end(); ++ e) {
				auto ulabel = status.GRAPH_IS_MIN[e->dst].label;
				if (vlabel <= ulabel)
					emb_lists[vlabel][ulabel].push(2, &(*e), 0);
			}
		}
		auto fromlabel = emb_lists.begin();
		auto tolabel = fromlabel->second.begin();
		status.DFS_CODE_IS_MIN.push(0, 1, fromlabel->first, 0, tolabel->first);
		return subgraph_is_min(status, tolabel->second);
	}
	bool subgraph_is_min(Status &status, BaseEdgeEmbeddingList &emb_list) {
		const RMPath& rmpath = status.DFS_CODE_IS_MIN.buildRMPath();
		auto minlabel        = status.DFS_CODE_IS_MIN[0].fromlabel;
		auto maxtoc          = status.DFS_CODE_IS_MIN[rmpath[0]].to;
		//if (debug) std::cout << "\t[IS_MIN] DFSCode: " << status.DFS_CODE_IS_MIN << "\n";
		//if (debug) std::cout << "\t[IS_MIN] Number of embeddings: " << emb_list.size() << "\n";
		//if (debug) {
		//	for(size_t i = 0; i < emb_list.size(); i++)
		//	std::cout << "\t[IS_MIN] EmbeddingList (num_vertices=" << emb_list[i].num_vertices << "): " << emb_list[i].to_string_all() << "\n";
		//}
		//if (debug) std::cout << "\t[IS_MIN] minlabel: " << minlabel << "  maxtoc: " << maxtoc << "\n";
	
		// backward
		bool found = false;
		VeridT newto = 0;
		BaseEdgeEmbeddingList emb_list_bck;
		for(size_t i = rmpath.size()-1; i >= 1; -- i) {
			for(size_t j = 0; j < emb_list.size(); ++ j) {
				BaseEdgeEmbedding *cur = &emb_list[j];
				History history(cur);
				auto e1 = history[rmpath[i]];
				auto e2 = history[rmpath[0]];
				if (e1 == e2) continue;
				for (auto e = status.GRAPH_IS_MIN[e2->dst].edge.begin(); e != status.GRAPH_IS_MIN[e2->dst].edge.end(); ++ e) {
					if (history.hasEdge(e->src, e->dst)) continue;
					if ((e->dst == e1->src) && (status.GRAPH_IS_MIN[e1->dst].label <= status.GRAPH_IS_MIN[e2->dst].label)) {
						emb_list_bck.push(2, &(*e), cur);
						newto = status.DFS_CODE_IS_MIN[rmpath[i]].from;
						found = true;
						break;
					}
				}
			}
		}
		if(found) {
			status.DFS_CODE_IS_MIN.push(maxtoc, newto, (LabelT)-1, 0, (LabelT)-1);
			auto size = status.DFS_CODE_IS_MIN.size() - 1;
			if (status.DFS_CODE[size] != status.DFS_CODE_IS_MIN[size]) return false;
			return subgraph_is_min(status, emb_list_bck);
		}

		// forward
		bool flg = false;
		VeridT newfrom = 0;
		EmbeddingLists1D emb_lists_fwd;
		for (size_t n = 0; n < emb_list.size(); ++n) {
			BaseEdgeEmbedding *cur = &emb_list[n];
			History history(cur);
			auto e2 = history[rmpath[0]];
			for (auto e = status.GRAPH_IS_MIN[e2->dst].edge.begin(); e != status.GRAPH_IS_MIN[e2->dst].edge.end(); ++ e) {
				if (minlabel > status.GRAPH_IS_MIN[e->dst].label || history.hasVertex(e->dst)) continue;
				if (flg == false) {
					flg = true;
					newfrom = maxtoc;
				}
				emb_lists_fwd[status.GRAPH_IS_MIN[e->dst].label].push(2, &(*e), cur);
			}
		}
		for (size_t i = 0; !flg && i < rmpath.size(); ++i) {
			for (size_t n = 0; n < emb_list.size(); ++n) {
				BaseEdgeEmbedding *cur = &emb_list[n];
				History history(cur);
				auto e1 = history[rmpath[i]];
				for (auto e = status.GRAPH_IS_MIN[e1->src].edge.begin(); e != status.GRAPH_IS_MIN[e1->src].edge.end(); ++ e) {
					auto dst = e->dst;
					auto &v = status.GRAPH_IS_MIN[dst];
					if (e1->dst == dst || minlabel > v.label || history.hasVertex(dst)) continue;
					if (status.GRAPH_IS_MIN[e1->dst].label <= v.label) {
						if (flg == false) {
							flg = true;
							newfrom = status.DFS_CODE_IS_MIN[rmpath[i]].from;
						}
						emb_lists_fwd[v.label].push(2, &(*e), cur);
					}
				}
			}
		}
		if (flg) {
			auto tolabel = emb_lists_fwd.begin();
			status.DFS_CODE_IS_MIN.push(newfrom, maxtoc + 1, (LabelT)-1, 0, tolabel->first);
			auto size = status.DFS_CODE_IS_MIN.size() - 1;
			if (status.DFS_CODE[size] != status.DFS_CODE_IS_MIN[size]) return false;
			return subgraph_is_min(status, tolabel->second);
		} 
		return true;
	} // end subgraph_is_min
	unsigned support(BaseEdgeEmbeddingList &emb_list, Status &status) {
		#ifdef PRECOMPUTE
		if (status.current_dfs_level == 0) return threshold;
		#endif
		Map2D vid_counts;
		for(auto cur = emb_list.begin(); cur != emb_list.end(); ++cur) {
			BaseEdgeEmbedding* emb_ptr = &(*cur);
		//for(auto emb : emb_list) {
		//	auto emb_ptr = &emb;
			size_t index = status.DFS_CODE.size() - 1;
			while (emb_ptr != NULL) {
				auto from = status.DFS_CODE[index].from;
				auto to = status.DFS_CODE[index].to;
				auto src = emb_ptr->edge->src;
				auto dst = emb_ptr->edge->dst;
				if (to > from) vid_counts[to][dst]++; //forward edge
				if (!emb_ptr->prev) vid_counts[from][src]++; // last element
				emb_ptr = emb_ptr->prev;
				index--;
			}
		}
		unsigned min = 0xffffffff;
		for(auto it = vid_counts.begin(); it != vid_counts.end(); it++)
			if((it->second).size() < min) min = (it->second).size();
		if(min == 0xffffffff) min = 0;
		return min;
	}
	inline InitPattern get_init_pattern(BYTE src_label, BYTE dst_label) {
		if (src_label <= dst_label) return std::make_pair(src_label, dst_label);
		else return std::make_pair(dst_label, src_label);
	}
	bool is_quick_automorphism(unsigned size, const EdgeEmbedding& emb, BYTE history, VertexId src, VertexId dst, BYTE& existed) {
		if (dst <= emb.get_vertex(0)) return true;
		if (dst == emb.get_vertex(1)) return true;
		if (history == 0 && dst < emb.get_vertex(1)) return true;
		if (size == 2) {
		} else if (size == 3) {
			if (history == 0 && emb.get_history(2) == 0 && dst <= emb.get_vertex(2)) return true;
			if (history == 0 && emb.get_history(2) == 1 && dst == emb.get_vertex(2)) return true;
			if (history == 1 && emb.get_history(2) == 1 && dst <= emb.get_vertex(2)) return true;
			if (dst == emb.get_vertex(2)) existed = 1;
		} else {
			std::cout << "Error: should go to detailed check\n";
		}
		return false;
	}
	bool is_edge_automorphism(unsigned size, const EdgeEmbedding& emb, BYTE history, VertexId src, VertexId dst, BYTE& existed, const VertexSet& vertex_set) {
		if (size < 3) return is_quick_automorphism(size, emb, history, src, dst, existed);
		if (dst <= emb.get_vertex(0)) return true;
		if (history == 0 && dst <= emb.get_vertex(1)) return true;
		if (dst == emb.get_vertex(emb.get_history(history))) return true;
		if (vertex_set.find(dst) != vertex_set.end()) existed = 1;
		if (existed && src > dst) return true;
		std::pair<VertexId, VertexId> added_edge(src, dst);
		for (unsigned index = history + 1; index < emb.size(); ++index) {
			std::pair<VertexId, VertexId> edge;
			edge.first = emb.get_vertex(emb.get_history(index));
			edge.second = emb.get_vertex(index);
			int cmp = compare(added_edge, edge);
			if(cmp <= 0) return true;
		}
		return false;
	}
	inline void swap(std::pair<VertexId, VertexId>& pair) {
		if (pair.first > pair.second) {
			VertexId tmp = pair.first;
			pair.first = pair.second;
			pair.second = tmp;
		}
	}
	inline int compare(std::pair<VertexId, VertexId>& oneEdge, std::pair<VertexId, VertexId>& otherEdge) {
		swap(oneEdge);
		swap(otherEdge);
		if(oneEdge.first == otherEdge.first) return oneEdge.second - otherEdge.second;
		else return oneEdge.first - otherEdge.first;
	}
};

#endif
