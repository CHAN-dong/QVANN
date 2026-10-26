"""Generate a separate LUT variant; preserve the published scalar benchmark."""
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
STUDY=ROOT/'experiments/ours-lut-20261015'


def main():
    base=ROOT/'src/experiments/veriann_optimized_benchmark.cpp'
    header=ROOT/'src/veriann_query_lut.hpp'
    code=base.read_text()
    start=code.index('struct Codec {');end=code.index('struct Walk {')
    scalar=re.sub(r'\bCodec\b','CodecScalar',code[start:end])
    code=code[:start]+scalar+'#include "'+str(header)+'"\n'+code[end:]
    original='    Walk t;std::map<Id,float> scores;'
    assert code.count(original)==1
    # Both server and independent client build their own tables. Cost is included
    # in search_ms / verification_ms because prepare runs inside walk().
    code=code.replace(original,'    codec.prepare(q);\n'+original)
    code=code.replace('#include "refinement_merkle_ordering.cpp"',
                      '#include "'+str(ROOT/'src/experiments/refinement_merkle_ordering.cpp')+'"')
    code=code.replace('#include "../paper_repro_common.hpp"',
                      '#include "'+str(ROOT/'src/paper_repro_common.hpp')+'"')
    code=code.replace('"VeriANN-optimized",ads_bytes','"VeriANN-LUT",ads_bytes')
    output=ROOT/'build/veriann_lut_benchmark.cpp';output.write_text(code)
    STUDY.mkdir(parents=True,exist_ok=True)
    metadata={'base_source':str(base),'base_sha256':hashlib.sha256(base.read_bytes()).hexdigest(),
              'lut_header':str(header),'lut_header_sha256':hashlib.sha256(header.read_bytes()).hexdigest(),
              'generated_source':str(output),'generated_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
              'changes':['per-query SQ4 score / SQ4 and SQ6 lower-bound lookup tables',
                         'prepare cost included in both server and client timers'],
              'unchanged':['wire format','Merkle trees','traversal','E and k','graph cache',
                           'per-dimension accumulation order','nextafter boundary rounding']}
    (STUDY/'variant.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(json.dumps({'generated':str(output)}))


if __name__=='__main__':main()
