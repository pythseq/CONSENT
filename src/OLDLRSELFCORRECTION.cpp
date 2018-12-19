#include <fstream>
#include <iostream>
#include <ctime>
#include <cmath>
#include <chrono>
#include "LRSelfCorrection.h"
#include "localAlignment.h"
#include "kMersProcessing.h"
#include "DBG.h"
#include "../BMEAN/bmean.h"
#include "../BMEAN/utils.h"

std::mutex outMtx;

bool compareLen(const std::string& a, const std::string& b) {
    return (a.size() > b.size()); 
}

std::vector<std::string> removeBadSequencesPrev(std::vector<std::string>& sequences, std::string tplSeq, unsigned merSize, unsigned commonKMers, unsigned solidThresh, unsigned windowSize) {
	std::unordered_map<std::string, std::vector<unsigned>> kMers = getKMersPos(tplSeq, merSize);
	std::unordered_map<std::string, unsigned> merCounts = getKMersCounts(sequences, merSize);
	std::string curSeq;
	unsigned i, j, c;
	int pos, tplBegPos, tplEndPos, curSeqBegPos, curSeqEndPos;
	std::set<std::string> anchored;
	std::string mer;
	std::unordered_map<int, std::vector<std::string>> unordered_mapCommonMers;

	// Iterate through the sequences of the alignment window, and only keep those sharing enough solid k-mers with the template
	i = 1;
	while (i < sequences.size()) {
		curSeq = sequences[i];
		j = 0;
		c = 0;
		anchored.clear();
		pos = -1;
		tplBegPos = -1;
		tplEndPos = -1;
		curSeqBegPos = -1;
		curSeqEndPos = -1;

		std::vector<std::string> smallerMSAs;

		// Allow overlapping k-mers
		// while (j < curSeq.length() - merSize + 1) {
		// 	mer = (curSeq.substr(j, merSize));
		// 	bool found = false;
		// 	unsigned p = 0;
		// 	// Check if the current k-mer (of the current sequence) appears in the template sequence after the current position
		// 	while (!found and p < kMers[mer].size()) {
		// 		found = pos == -1 or kMers[mer][p] > pos;
		// 		p++;
		// 	}
		// 	// Allow repeated k-mers
		// 	// if (merCounts[mer] >= solidThresh and found) {
		// 	// Non-repeated k-mers only
		// 	if (merCounts[mer] >= solidThresh and found and anchored.find(mer) == anchored.end() and kMers[mer].size() == 1) {
		// 		pos = kMers[mer][p-1];
		// 		if (tplBegPos == -1) {
		// 			tplBegPos = pos;
		// 			curSeqBegPos = j;
		// 		}

		// 		c++;
		// 		j += 1;
		// 		anchored.insert(mer);
		// 		tplEndPos = pos + merSize - 1;
		// 		curSeqEndPos = j + merSize - 2;
		// 	} else {
		// 		j += 1;
		// 	}
		// }

		// Non-overlapping k-mers only
		while (j < curSeq.length() - merSize + 1) {
			mer = (curSeq.substr(j, merSize));
			bool found = false;
			unsigned p = 0;
			while (!found and p < kMers[mer].size()) {
				found = pos == -1 or kMers[mer][p] >= pos + merSize;
				p++;
			}
			// Allow repeated k-mers
			if (merCounts[mer] >= solidThresh and found) {
			// Non-repeated k-mers only
			// if (merCounts[mer] >= solidThresh and found and anchored.find(mer) == anchored.end() and kMers[mer].size() == 1) {
				pos = kMers[mer][p-1];
				if (tplBegPos == -1) {
					tplBegPos = pos;
					curSeqBegPos = j;
				}

				c++;
				j += merSize;
				anchored.insert(mer);
				tplEndPos = pos + merSize - 1;
				curSeqEndPos = j - 1;
			} else {
				j += 1;
			}
		}

		if (c >= commonKMers) {
			int beg, end;
            beg = std::max(0, curSeqBegPos - tplBegPos);
            end = std::min(curSeqEndPos + tplSeq.length() - tplEndPos - 1, curSeq.length() - 1);
            std::string tmpSeq = sequences[i].substr(beg, end + 1);

			// Only add the substring from the leftmost anchor to the rightmost anchor to the MSA (anchor = shared k-mer wth the template)
			unordered_mapCommonMers[c].push_back(tmpSeq);
			// Add the whole sequence to the MSA
			// unordered_mapCommonMers[c].push_back(sequences[i]);
		}

		i++;
	}

	// Insert sequences from the alignment pile into the result vector according to their number of shared k-mers with the template sequence
	// (useful to compute consensus quicker with POA). If two sequences have the same numer of shared k-mer, sort them according to their length.
	// Both sorts are done in descending order.
	std::vector<std::string> newSequences;
	newSequences.push_back(sequences[0]);
	std::vector<std::string> tmpNewSeqs;
	for (std::unordered_map<int, std::vector<std::string>>::reverse_iterator it = unordered_mapCommonMers.rbegin(); it != unordered_mapCommonMers.rend(); it++) {
		tmpNewSeqs.clear();
		for (std::string s : it->second) {
			tmpNewSeqs.push_back(s);
		}
		std::sort(tmpNewSeqs.begin(), tmpNewSeqs.end(), compareLen);
		for (std::string s : tmpNewSeqs) {
			newSequences.push_back(s);
		}
	}

	return newSequences;
}

void toUpperCase(std::string& s, int beg, int end) {
	std::locale loc;
	for (int i = beg; i < end; i++) {
		s[i] = std::toupper(s[i], loc);
	}
}

void toLowerCase(std::string& s, int beg, int end) {
	std::locale loc;
	for (int i = beg; i < end; i++) {
		s[i] = std::tolower(s[i], loc);
	}
}



std::string weightConsensus(std::string& consensus, std::vector<std::string> pile, unsigned merSize, unsigned windowSize, unsigned solidThresh) {
	std::vector<std::string> splits;
	std::string curSplit;
	std::unordered_map<std::string, unsigned> merCounts = getKMersCounts(pile, merSize);

	std::string header = "";
	std::string sequence = "";

	unsigned i = 0;
	while (i < consensus.length() - merSize + 1) {
		std::string curFct = consensus.substr(i, merSize);
		toUpperCase(curFct, 0, merSize);
		if (merCounts[curFct] >= solidThresh) {
			toUpperCase(consensus, i, i + merSize - 1);
		} else {
			toLowerCase(consensus, i, i + merSize - 1);
		}
		i++;
	}

	return consensus;
}

std::vector<std::pair<std::string, std::string>> computeConsensuses(std::string readId, std::vector<std::vector<std::string>>& piles, std::vector<std::pair<unsigned, unsigned>>& pilesPos, std::string readsDir, unsigned minSupport, unsigned merSize, unsigned commonKMers, unsigned solidThresh, unsigned windowSize) {
	std::vector<std::pair<std::string, std::string>> res;

	if (piles.size() == 0) {
		return res;
	}

	// Split strings to compute consensuses quicker
	std::vector<std::vector<std::string>> result = MSABMAAC(piles[0], 8, 0.5);
	std::vector<std::vector<std::string>> splits;
	std::vector<std::string> curSplit;

	std::cerr << "result size " << result.size() << std::endl;

	int nbSeqPerSplit = result[0].size();
	std::vector<std::string> globalMSA(, "");

	// Prepare POA input file
	std::string POAinFile = readsDir + "../POAInput/" + readId;
	const char* cPOAinFile = POAinFile.c_str();
	std::ofstream inPOA(POAinFile);
	int nbReg = 0;
	for (std::vector<std::string> p : result) {
		unsigned mySize = (int) p.size();
		std::cerr << "support : " << p.size() << std::endl;
		// mySize = std::min(15, (int) p.size());
		inPOA << mySize << std::endl;
		nbReg = 0;
		for (unsigned i = 0; i < mySize; i++) {
			std::string s = p[i];
			inPOA << ">" << nbReg << std::endl << s << std::endl;
			nbReg++;
		};
		std::cerr << "nbReg : " << nbReg << std::endl;
	}
	inPOA.close();

	// Run POA for every split
	std::string POAoutFile = readsDir + "../POAOutput/" + readId;
	const char* cPOAOutFile = POAoutFile.c_str();
	std::string cmd = "poaV2/poa -read_fasta " + POAinFile + " -lower -hb -best -pir " + POAoutFile + " poaV2/blosum80.mat > /dev/null 2> /dev/null";
	const char *ccmd = cmd.c_str();
	if (system(ccmd) == -1) {
		exit(EXIT_FAILURE);
	}
	auto c_start = std::chrono::high_resolution_clock::now();
	auto c_end = std::chrono::high_resolution_clock::now();
	remove(cPOAinFile);
	std::cerr << "POA took " << std::chrono::duration_cast<std::chrono::milliseconds>(c_end - c_start).count() << " ms\n";

	// Store templates corrections in the result vector
	res.clear();
	std::ifstream cons(POAoutFile);
	std::string consHeader = "";
	std::string consSeq = "";
	std::string finalCons = "";

	// Concatenate consensus of the splits into the final consensus
	while (getline(cons, consHeader)) {
		getline(cons, consSeq);
		finalCons += consSeq;
		std::cerr << ">cons" << std::endl << consSeq << std::endl;
	}

	std::cerr << "finalCons size : " << finalCons.size() << std::endl;

	// Align the computed consensus to the template, to retrieve the (raw, corrected) pairs of subsequences
	if (!finalCons.empty()) {
		int i = 0;
		c_start = std::chrono::high_resolution_clock::now();
		std::pair<std::pair<int, int>, std::pair<int, int>> posPair = NeedlemanWunschLocalAlignments(piles[i][0], finalCons);
		c_end = std::chrono::high_resolution_clock::now();
		std::cerr << "Needleman took " << std::chrono::duration_cast<std::chrono::milliseconds>(c_end - c_start).count() << " ms\n";
		std::pair<int, int> rawPos = posPair.first;
		std::pair<int, int> corPos = posPair.second;
		std::string corTpl = finalCons.substr(corPos.first, corPos.second - corPos.first + 1);
		std::string rawTpl = piles[i][0].substr(rawPos.first, rawPos.second - rawPos.first + 1);
		std::cerr << "corTpl size : " << corTpl.size() << std::endl;
		// std::string rawTpl = piles[i][0];
		corTpl = weightConsensus(corTpl, piles[i], merSize, windowSize, solidThresh);
		std::cerr << ">corTpl" << std::endl << corTpl << std::endl;
		res.push_back(std::make_pair(rawTpl, corTpl));
		i++;
	}
	cons.close();
	remove(cPOAOutFile);

	c_end = std::chrono::high_resolution_clock::now();

	std::cerr << "returns" << std::endl;

	return res;
}

std::pair<std::string, std::vector<std::pair<std::pair<int, int>, int>>> alignConsensuses(std::string rawRead, std::unordered_map<std::string, std::string>& sequences, std::vector<std::pair<std::string, std::string>>& consensuses, std::vector<std::pair<unsigned, unsigned>> pilesPos, std::vector<std::vector<std::string>> piles, int startPos) {
	std::vector<std::pair<std::string, std::string>> correctedReads;
	int beg, end;
	std::string sequence;
	sequence = sequences[rawRead];
	std::transform(sequence.begin() + startPos, sequence.end(), sequence.begin() + startPos, ::tolower);
	std::vector<std::pair<std::pair<int, int>, int>> corPosPiles;

	// Anchor the corrected templates on the read
	std::string corWindow;
	unsigned i = 0;
	for (i = 0; i < consensuses.size(); i++) {
		std::pair<std::string, std::string> c = consensuses[i];
		// Don't proceed if no consensus was produced by POA
		if (!c.second.empty()) {
			// Replace the template by its correction on the read
			std::transform(c.first.begin(), c.first.end(), c.first.begin(), ::tolower);
			std::string tmpSequence = sequence;
			std::transform(tmpSequence.begin(), tmpSequence.end(), tmpSequence.begin(), ::tolower);
			beg = (int) tmpSequence.find(c.first);
			end = beg + c.first.length() - 1;
			if ((int) beg != -1) {
				sequence.replace(beg, c.first.length(), c.second);
			}
			end = beg + c.second.length() - 1;
		}
		// Specify that positions beg..end of the corrected read correspond to the current pile
		corPosPiles.push_back(std::make_pair(std::make_pair(beg, end), i));
	}

	return std::make_pair(sequence, corPosPiles);
}

bool isUpperCase(char c) {
	return 'A' <= c and c <= 'Z';
}

int getNextSrc(std::string correctedRead, unsigned beg, unsigned merSize) {
	unsigned nb = 0;
	unsigned i = beg;

	while (i < correctedRead.length() and (isUpperCase(correctedRead[i]) or nb < merSize)) {
		if (isUpperCase(correctedRead[i])) {
			nb++;
		} else {
			nb = 0;
		}
		i++;
	}

	return nb >= merSize ? i - 1 : -1;
}

int getNextDst(std::string correctedRead, unsigned beg, unsigned merSize) {
	unsigned nb = 0;
	unsigned i = beg;

	while (i < correctedRead.length() and nb < merSize) {
		if (isUpperCase(correctedRead[i])) {
			nb++;
		} else {
			nb = 0;
		}
		i++;
	}

	return nb >= merSize ? i - 1 : -1;
}

std::pair<std::vector<std::string>, int> getRegPiles(std::vector<std::pair<std::pair<int, int>, int>> corPosPiles, std::vector<std::vector<std::string>> piles, int beg, int end, int lastPile) {
	int pileBeg, pileEnd;
	std::vector<std::string> res;

	unsigned i = lastPile;
	while (i < corPosPiles.size() and corPosPiles[i].first.first <= end) {
		pileBeg = corPosPiles[i].first.first;
		pileEnd = corPosPiles[i].first.second;

		if ((pileBeg <= beg and beg <= pileEnd) or (pileBeg <= end and end <= pileEnd)) {
			for (unsigned j = 0; j < piles[corPosPiles[i].second].size(); j++) {
				// Push current pile, as it spans the required positions
				res.push_back(piles[corPosPiles[i].second][j]);

				// Push previous and following piles, as they can contain information
				// if (j - 1 >= 0) {
				// 	res.push_back(piles[corPosPiles[i].second][j-1]);
				// }
				// if (j + 1 < piles[corPosPiles[i].second].size()) {
				// 	res.push_back(piles[corPosPiles[i].second][j+1]);
				// }
			}
		}
		i++;
	}

	return std::make_pair(res, i - 1);
}

std::vector<std::string> removeUnanchoredPiles(std::vector<std::string>& regPiles, std::string src, std::string dst, unsigned merSize) {
	std::vector<std::string> res;

	for (std::string s : regPiles) {
		std::unordered_map<std::string, std::vector<unsigned>> mersPos = getKMersPos(s, merSize);

		std::vector<unsigned> posSrc = mersPos[src];
		std::vector<unsigned> posDst = mersPos[dst];

		if (!posSrc.empty() or !posDst.empty()) {
			res.push_back(s);
		}

		// bool found = false;
		// int p, q;			
		// if (!posSrc.empty() and !posDst.empty()) {
		// 	p = 0;
		// 	found = false;
		// 	while (!found and p < posSrc.size()) {
		// 		q = 0;
		// 		while (!found and q < posDst.size()) {
		// 			found = posDst[q] > posSrc[p];
		// 			q++;
		// 		}
		// 		p++;
		// 	}

		// 	if (found) {
		// 		// res.push_back(s.substr(posSrc[p-1], posDst[q-1] - posSrc[p-1] + merSize));
		// 		res.push_back(s);
		// 	}
		// }
	}

	return res;
}

std::pair<int, int> getNextBadRegion(std::string correctedRead, unsigned beg) {
	int posBeg, posEnd;
	unsigned i = beg;

	while (i < correctedRead.length() and isUpperCase(correctedRead[i])) {
		i++;
	}
	posBeg = i;

	int nb = 0;
	while (i < correctedRead.length() and nb < 4) {
		if (isUpperCase(correctedRead[i])) {
			nb++;
		} else {
			nb = 0;
		}
		i++;
	}
	posEnd = i - nb - 1;

	return std::make_pair(posBeg, posEnd);
}


// Anchors without repeated k-mers
std::vector<std::string> getAnchors(std::vector<std::string> pile, std::string region, unsigned merSize, int nb) {
	std::vector<std::string> res;

	// Get the counts of the k-mers of the pile
	std::unordered_map<std::string, unsigned> merCounts = getKMersCounts(pile, merSize);

	std::unordered_map<std::string, std::vector<unsigned>> mersPos = getKMersPos(region, merSize);

	// Consider all k-mers of the region as potential anchors
	std::vector<std::string> candidates;
	for (unsigned i = 0; i < region.size() - merSize + 1; i++) {
		candidates.push_back(region.substr(i, merSize));
	}

	// Sort the k-mers set in ascending order of number of occurrences
	std::sort(candidates.begin(), candidates.end(), 
		[&merCounts](std::string& n1, std::string& n2) {
			int occ1 = merCounts[n1];
			int occ2 = merCounts[n2];
			return  occ1 > occ2;
		}
	);

	// Add the nb most frequent k-mers to the result set
	unsigned i = 0;
	int n = 0;
	while (i < candidates.size() and n < nb) {
		if (mersPos[candidates[i]].size() == 1) {
			res.push_back(candidates[i]);
			n++;
		}
		i++;
	}

	return res;
}

std::vector<std::pair<std::string, std::string>> polishCorrection(std::string correctedRead, std::vector<std::pair<std::pair<int, int>, int>> corPosPiles, std::vector<std::vector<std::string>> piles, unsigned merSize, int solidThresh, int minGap, int maxGap) {
	std::unordered_map<std::string, int> merCounts;
	std::unordered_map<int, std::unordered_map<std::string, unsigned>> unordered_mapMerCounts;
	std::set<std::string> visited;
	unsigned curBranches;
	unsigned dist;
	std::string curExt;
	std::string correctedRegion;
	unsigned maxSize;
	unsigned maxBranches = 50;
	std::vector<std::pair<std::string, std::string>> corList;

	int zone = 3;

	// Skip uncorrected head of the read
	unsigned i = 0;
	while (i < correctedRead.length() and !isUpperCase(correctedRead[i])) {
		i++;
	}

	int srcBeg, srcEnd, dstBeg, dstEnd, k;
	int tmpSrcBeg, tmpSrcEnd, tmpDstBeg, tmpDstEnd;
	std::string src, dst;
	int lastPile = 0;
	// search for poorly supported regions bordered by solid corrected regions
	while (i < correctedRead.length()) {
		std::pair<int, int> pos = getNextBadRegion(correctedRead, i);
		// std::cerr << "bad region is : " << correctedRead.substr(pos.first, pos.second - pos.first + 1) << std::endl;
		srcEnd = getNextSrc(correctedRead, i, merSize + zone);
		dstEnd = getNextDst(correctedRead, srcEnd + 1, merSize + zone);
		srcBeg = srcEnd - merSize - zone + 1;
		dstBeg = dstEnd - merSize - zone + 1;

		k = zone;
		tmpSrcBeg = srcBeg + k;
		tmpSrcEnd = tmpSrcBeg + merSize - 1;
		tmpDstBeg = dstBeg + merSize - k;
		tmpDstEnd = tmpDstBeg + merSize - 1;

		// Polish the poorly supported region region if 2 anchors were found
		if (srcEnd != -1 and dstEnd != -1) {
			if (minGap <= dstBeg - srcEnd - 1 && dstBeg - srcEnd - 1 <= maxGap) {
				// Retrieve sequences of the windows overlapping to the region to polish
				std::pair<std::vector<std::string>, int> p = getRegPiles(corPosPiles, piles, srcBeg, dstEnd, lastPile);
				std::vector<std::string> regPiles = p.first;
				lastPile = p.second;
				std::vector<std::string> oldRegPiles = regPiles;
				// regPiles = removeUnanchoredPiles(regPiles, src, dst, merSize);

				// std::cerr << "regpiles size : " << regPiles.size() << std::endl;

				// Get the k-mers of the windows sequences
				int minOrder = merSize;
				for (int j = merSize; j >= minOrder; j--) {
					unordered_mapMerCounts[j] = getKMersCounts(regPiles, j);
				}

				// std::cerr << "got sequences" << std::endl;

				// Attempt to link the anchors that are closest to the region to polish
				correctedRegion = "";
				src = correctedRead.substr(tmpSrcBeg, merSize);
				dst = correctedRead.substr(tmpDstBeg, merSize);
				if (src != dst) {
					visited.clear();
					curBranches = 0;
					dist = 0;
					curExt = src;
					correctedRegion = "";
					maxSize = 15.0 / 100.0 * 2.0 * (tmpDstBeg - tmpSrcEnd - 1) + (tmpDstBeg - tmpSrcEnd - 1) + merSize;
					// std::cerr << "Linking : " << src << " to " << dst << std::endl;
					link(unordered_mapMerCounts, src, dst, merSize, visited, &curBranches, dist, curExt, correctedRegion, merSize, maxSize, maxBranches, solidThresh, minOrder);
					// std::cerr << "Result is : " << correctedRegion << std::endl;

					// Link from tgt to src, inefficient in practice
					// if (correctedRegion.empty()) {
					// 	visited.clear();
					// 	curBranches = 0;
					// 	dist = 0;
					// 	curExt = rev_comp::run(dst);
					// 	correctedRegion = "";
					// 	maxSize = 15.0 / 100.0 * 2.0 * (tmpDstBeg - tmpSrcEnd - 1) + (tmpDstBeg - tmpSrcEnd - 1) + merSize;
					// 	std::cerr << "RCLinking : " << src << " to " << dst << std::endl;
					// 	link(unordered_mapMerCounts, rev_comp::run(dst), rev_comp::run(src), merSize, visited, &curBranches, dist, curExt, correctedRegion, merSize, maxSize, maxBranches, solidThresh, minOrder);
					// 	std::cerr << "RC bResult is : " << correctedRegion << std::endl;						
					// }
				}


				if (correctedRegion.empty()) {
					// Compute frequent anchors to try to link again if first linking fails
					std::vector<std::string> srcList = getAnchors(regPiles, correctedRead.substr(srcBeg, merSize + zone), merSize, zone + 1);
					std::vector<std::string> dstList = getAnchors(regPiles, correctedRead.substr(dstBeg, merSize + zone), merSize, zone + 1);
					std::vector<std::pair<std::string, std::string>> anchors;				
					for (std::string s : srcList) {
						for (std::string d : dstList) {
							if (s != src or d != dst) {
								anchors.push_back(std::make_pair(s, d));
							}
						}
					}
					std::unordered_map<std::string, std::vector<unsigned>> srcPos = getKMersPos(correctedRead.substr(srcBeg, merSize + zone), merSize);
					std::unordered_map<std::string, std::vector<unsigned>> dstPos = getKMersPos(correctedRead.substr(dstBeg, merSize + zone), merSize);
					

					// Attempt to link frequent anchors
					unsigned anchorNb = 0;
					while (anchorNb < anchors.size() and minGap <= tmpDstBeg - tmpSrcEnd - 1 and tmpDstBeg - tmpSrcEnd - 1 <= maxGap and correctedRegion.empty()) {
						src = anchors[anchorNb].first;
						dst = anchors[anchorNb].second;
						tmpSrcBeg = srcBeg + srcPos[src][0];
						tmpSrcEnd = tmpSrcBeg + merSize - 1;
						tmpDstBeg = dstBeg + dstPos[dst][0];
						tmpDstEnd = tmpDstBeg + merSize - 1;
						
						if (src != dst) {
							visited.clear();
							curBranches = 0;
							dist = 0;
							curExt = src;
							correctedRegion = "";
							maxSize = 15.0 / 100.0 * 2.0 * (tmpDstBeg - tmpSrcEnd - 1) + (tmpDstBeg - tmpSrcEnd - 1) + merSize;
							// std::cerr << "Linking : " << src << " to " << dst << std::endl;
							link(unordered_mapMerCounts, src, dst, merSize, visited, &curBranches, dist, curExt, correctedRegion, merSize, maxSize, maxBranches, solidThresh, minOrder);
							// std::cerr << "Result is : " << correctedRegion << std::endl;
						}
						anchorNb++;
					}
				}

				if (!correctedRegion.empty()) {
					corList.push_back(std::make_pair(correctedRead.substr(tmpSrcBeg, tmpDstEnd - tmpSrcBeg + 1), correctedRegion));
				}
			}
			i = tmpDstBeg;
		} else {
			i = correctedRead.length();
		}
	}

	// std::cerr << "return" << std::endl;
	return corList;
}

int nbCorBases(std::string correctedRead) {
	int n = 0;
	for (unsigned i = 0; i < correctedRead.length(); i++) {
		if ('A' <= correctedRead[i] && correctedRead[i] <= 'Z') {
			n++;
		}
	}

	return n;
}

bool dropRead(std::string correctedRead) {
	return (float) nbCorBases(correctedRead) / correctedRead.length() < 0.6;
}

std::string trimRead(std::string correctedRead, unsigned merSize) {
	unsigned beg, end, n;
	int i;
	i = 0;
	n = 0;
	while (i < correctedRead.length() and n < merSize) {
		if (isUpperCase(correctedRead[i])) {
			n++;
		} else {
			n = 0;
		}
		i++;
	}
	beg = i - merSize + 1;

	i = correctedRead.length() - 1;
	n = 0;
	while (i >= 0 and n < merSize) {
		if (isUpperCase(correctedRead[i])) {
			n++;
		} else {
			n = 0;
		}
		i--;
	}
	end = i + merSize - 1;

	if (end > beg) {
		return correctedRead.substr(beg, end - beg + 1);
	} else {
		return "";
	}
}

void processRead(std::vector<Alignment>& alignments, std::string readsDir, unsigned minSupport, unsigned windowSize, unsigned merSize, unsigned commonKMers, unsigned solidThresh, unsigned windowOverlap) {
	std::string readId = alignments.begin()->qName;

	// Compute alignment piles
	std::unordered_map<std::string, std::string> sequences = getSequencesunordered_maps(alignments, readsDir);
	std::pair<std::vector<std::pair<unsigned, unsigned>>, std::vector<std::vector<std::string>>> pairPiles = getAlignmentPiles(alignments, minSupport, windowSize, windowOverlap, sequences);
	std::vector<std::vector<std::string>> piles = pairPiles.second;
	std::vector<std::pair<unsigned, unsigned>> pilesPos = pairPiles.first;

	// Remove sequences sharing too few solid k-mers with their associated template
	std::vector<std::vector<std::string>> oldPiles = piles;
	unsigned i = 0;
	while (i < piles.size()) {
		std::string tpl = piles[i][0];

		piles[i] = removeBadSequencesPrev(piles[i], piles[i][0], merSize, commonKMers, solidThresh, windowSize);
		if (piles[i].size() < minSupport) {
			piles.erase(piles.begin() + i);
			pilesPos.erase(pilesPos.begin() + i);
		} else {
			i++;
		}
	}

	// Compute consensuses for all the piles at once
	std::vector<std::pair<std::string, std::string>> consensuses;

	for (i = 0; i < piles.size(); i++) {
		std::vector<std::pair<unsigned, unsigned>> curPos;
		curPos.push_back(pilesPos[i]);

		std::vector<std::vector<std::string>> curPiles;
		curPiles.push_back(piles[i]);
		// std::cerr << "call consensuses" << std::endl;
		std::vector<std::pair<std::string, std::string>> curCons = computeConsensuses(readId, curPiles, curPos, readsDir, minSupport, merSize, commonKMers, solidThresh, windowSize);
		if (!curCons.empty()) {
			consensuses.push_back(curCons[0]);
		}
		// std::cerr << "gets consensues" << std::endl;
	}

	// std::cerr << "computed all consensuses" << std::endl;
	
	// Align computed consensuses to the read and retrieve poorly supported regions windows positions
	std::pair<std::string, std::vector<std::pair<std::pair<int, int>, int>>> p = alignConsensuses(readId, sequences, consensuses, pilesPos, piles, 0);
	std::string correctedRead = p.first;
	std::vector<std::pair<std::pair<int, int>, int>> corPosPiles = p .second;

	// Drop read if it contains too many poorly supported bases
	// if (!dropRead(correctedRead)) {
	if (1) {
		// Polish poorly supported regions with local DBGs
		std::vector<std::pair<std::string, std::string>> corList, newList;

		// Polish small regions with a small k-mer size
		// newList = polishCorrection(correctedRead, corPosPiles, oldPiles, merSize, 2, 1, 2 * merSize);
		// for (auto p : newList) {
		// 	corList.push_back(p);
		// }

		// // Polish larger ergions with a larger k-mer size
		// newList = polishCorrection(correctedRead, corPosPiles, oldPiles, 2 * merSize, 2, 2 * merSize + 1, 100);
		// for (auto p : newList) {
		// 	corList.push_back(p);
		// }


		// Polish extremely large regions with a largerer k-mer size. Inefficient, would be better to split the read
		// newList = polishCorrection(correctedRead, corPosPiles, oldPiles, 3 * merSize, 1, 101, 300);
		// for (auto p : newList) {
		// 	corList.push_back(p);
		// }

		// Anchor the polished regions to the read (replace the original region by its polished version). Non-optimal, should be done online.
		for (std::pair<std::string, std::string> p : corList) {
			std::string r = p.first;
			std::string c = p.second;
			int b, l;
			b = correctedRead.find(r);
			l = r.length();
			if ((int) b != -1) {
				correctedRead.replace(b, l, c);
			}
		}

		// Trim the read (trims until a long sketch of corrected bases if found. Ex: aaaaCCggagtAttagGGACTTACGATCGATCGATCa => GGACTTACGATCGATCGATC)
		// correctedRead = trimRead(correctedRead, 100);
		if (!correctedRead.empty()) {
			outMtx.lock();
			std::cout << ">" << readId << std::endl << correctedRead << std::endl;
			outMtx.unlock();
		}
	}
}


void processReads(std::vector<std::vector<Alignment>>& reads, std::string readsDir, unsigned minSupport, unsigned windowSize, unsigned merSize, unsigned commonKMers, unsigned solidThresh, unsigned windowOverlap) {
	std::vector<std::pair<std::string, std::string>> consensuses;

	for (std::vector<Alignment> alignments : reads) {
		auto c_start = std::chrono::high_resolution_clock::now();
		processRead(alignments, readsDir, minSupport, windowSize, merSize, commonKMers, solidThresh, windowOverlap);
		auto c_end = std::chrono::high_resolution_clock::now();
		std::cerr << "processing " << alignments[0].qName << " took " << std::chrono::duration_cast<std::chrono::milliseconds>(c_end - c_start).count() << " ms\n";
	}

}

// Multithread ok, but running with GNU parallel for now.
// Multithreading seems to consume lots of resources when processing large number of reads.
void runCorrection(std::string alignmentFile, std::string readsDir, unsigned minSupport, unsigned windowSize, unsigned merSize, unsigned commonKMers, unsigned solidThresh, unsigned windowOverlap, unsigned nbThreads) {	
	std::ifstream f(alignmentFile);
	std::vector<Alignment> curReadAlignments;
	Alignment al;
	std::string curRead, line;
	curRead = "";
	int readNumber = 0;

	// Init threads
	std::vector<std::vector<std::vector<Alignment>>> reads;
	for (unsigned i = 0; i < nbThreads; i++) {
		reads.push_back(std::vector<std::vector<Alignment>>());
	}

	getline(f, line);
	while(line.length() > 0 or !curReadAlignments.empty()) {
		if (line.length() > 0) {
			al = Alignment(line);
		}
		if (line.length() > 0 and (curRead == "" or al.qName == curRead)) {
			curRead = al.qName;
			curReadAlignments.push_back(al);
			getline(f, line);
		} else {
			reads[readNumber % nbThreads].push_back(curReadAlignments);	
			readNumber++;
			curReadAlignments.clear();
			curRead = "";
		}
	}

	// Launch threads
	std::vector<std::future<void>> threads;
	for (unsigned i = 0 ; i < nbThreads ; i++) {
		std::vector<std::vector<Alignment>> als = reads[i];
		threads.push_back(async(std::launch::async, [als, readsDir, minSupport, windowSize, merSize, commonKMers, solidThresh, windowOverlap]() mutable {
			processReads(als, readsDir, minSupport, windowSize, merSize, commonKMers, solidThresh, windowOverlap);
		}));
	}
	
	// Get threads results
	for (std::future<void> &t: threads) {
		t.get();
	}
}