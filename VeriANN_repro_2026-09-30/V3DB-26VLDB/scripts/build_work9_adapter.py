"""Add wire export/verification hooks in a separate author-code checkout.

No search/circuit constraints are changed. Original upstream stays untouched.
"""
from pathlib import Path
import shutil
import subprocess

REPRO = Path(__file__).resolve().parents[1]
SOURCE = REPRO/'upstream'
DEST = REPRO/'work9-author'
if not DEST.exists():
    subprocess.run(['git','clone','--no-hardlinks',str(SOURCE),str(DEST)],check=True)
metrics = DEST/'src/utils/metrics.rs'
text = (SOURCE/'src/utils/metrics.rs').read_text()
needle = '    let verify_time = curr_time.elapsed().as_secs_f64(); // 秒'
assert needle in text
text = text.replace(needle, needle + '''

    // work9 instrumentation: export actual compressed proof and verifier data.
    // Export I/O is outside the author's returned timing measurements.
    if let Ok(folder) = std::env::var("V3DB_WIRE_DIR") {
        use plonky2::util::serialization::DefaultGateSerializer;
        std::fs::create_dir_all(&folder)?;
        std::fs::write(format!("{folder}/proof.bin"), &compressed_bytes)?;
        let verifier_bytes = data.verifier_data().to_bytes(&DefaultGateSerializer)
            .map_err(|e| anyhow::anyhow!("verifier serialization: {:?}", e))?;
        std::fs::write(format!("{folder}/verifier.bin"), verifier_bytes)?;
    }
''')
metrics.write_text(text)
lib = DEST/'src/lib.rs'
text = (SOURCE/'src/lib.rs').read_text()
needle = '    m.add_function(wrap_pyfunction!(py_set_based_gate, m)?)?;'
assert needle in text
text = text.replace(needle, needle+'''
    m.add_function(wrap_pyfunction!(py_work9_verify_wire, m)?)?;
    m.add_function(wrap_pyfunction!(py_work9_cluster_root, m)?)?;
    m.add_function(wrap_pyfunction!(py_work9_snapshot_roots, m)?)?;
    m.add_function(wrap_pyfunction!(py_work9_align_sets, m)?)?;
''')
text += '''

// work9-only adapter: verifier consumes wire bytes and pretrusted circuit data.
#[pyfunction]
pub fn py_work9_verify_wire(proof_bytes: Vec<u8>, verifier_bytes: Vec<u8>)
    -> PyResult<Vec<u64>> {
    use crate::prelude::{F, C, D};
    use plonky2::field::types::PrimeField64;
    use plonky2::plonk::circuit_data::VerifierCircuitData;
    use plonky2::plonk::proof::CompressedProofWithPublicInputs;
    use plonky2::util::serialization::DefaultGateSerializer;
    let verifier = VerifierCircuitData::<F, C, D>::from_bytes(verifier_bytes, &DefaultGateSerializer)
        .map_err(|e| PyRuntimeError::new_err(format!("verifier decode: {:?}", e)))?;
    let proof = CompressedProofWithPublicInputs::<F, C, D>::from_bytes(proof_bytes, &verifier.common)
        .map_err(|e| PyRuntimeError::new_err(format!("proof decode: {:?}", e)))?;
    let public_inputs = proof.public_inputs.iter().map(|x| x.to_canonical_u64()).collect();
    verifier.verify_compressed(proof)
        .map_err(|e| PyRuntimeError::new_err(format!("wire verification: {}", e)))?;
    Ok(public_inputs)
}

#[pyfunction]
pub fn py_work9_cluster_root(idx: i64, valids: Vec<i64>, items: Vec<i64>, codes: Vec<Vec<i64>>) -> u64 {
    crate::merkle_ver::ivf_pq_merkle::merkle_cluster_i64(idx, valids, items, codes)
}

#[pyfunction]
pub fn py_work9_snapshot_roots(centers: Vec<Vec<i64>>, books: Vec<Vec<Vec<i64>>>, roots: Vec<u64>) -> (u64, u64) {
    let leaves = centers.into_iter().enumerate().map(|(i,c)|
        crate::merkle_ver::circuit_based_proof::hash_ivf_center(i,c,roots[i])).collect();
    (crate::hash_gadgets::hash_tree_gen(leaves)[0],
     crate::merkle_ver::ivf_pq_merkle::commit_codebook_i64(books))
}

#[pyfunction]
pub fn py_work9_align_sets(f: Vec<Vec<i64>>, t: Vec<Vec<i64>>, alpha: u64) -> (Vec<u64>,Vec<u64>) {
    crate::ivf_pq_verify::proof::convert_ft_set_i64(f,t,alpha)
}
'''
lib.write_text(text)
alignment=DEST/'src/ivf_pq_verify/proof.rs'
text=(SOURCE/'src/ivf_pq_verify/proof.rs').read_text()
old='''    // 按规则调整t的位置
    for i in 1..sz {
        if f[i] != f[i - 1] {
            for j in 0..sz {
                if t[j] == f[i] {
                    t[j] = t[i];
                    t[i] = f[i];
                    break;
                }
            }
        }
    }
'''
new='''    // work9: preserve the exact first-match/swap ordering using indexed positions.
    // This changes host witness preparation only; circuit constraints are unchanged.
    use std::collections::{BTreeSet, HashMap};
    let mut positions: HashMap<u64, BTreeSet<usize>> = HashMap::new();
    for (i, &value) in t.iter().enumerate() {
        positions.entry(value).or_default().insert(i);
    }
    for i in 1..sz {
        if f[i] != f[i - 1] {
            let value = f[i];
            let first = positions.get(&value).and_then(|p| p.iter().next().copied());
            if let Some(j) = first {
                let old_value = t[i];
                if i != j && old_value != value {
                    let matches = positions.get_mut(&value).unwrap();
                    matches.remove(&j);
                    matches.insert(i);
                    let displaced = positions.get_mut(&old_value).unwrap();
                    displaced.remove(&i);
                    displaced.insert(j);
                    t[j] = old_value;
                    t[i] = value;
                }
            }
        }
    }
'''
assert old in text
alignment.write_text(text.replace(old,new))
patch = subprocess.check_output(['git','diff','--','src/lib.rs','src/utils/metrics.rs','src/ivf_pq_verify/proof.rs'],cwd=DEST)
(REPRO/'work9-adapter.patch').write_bytes(patch)
print('Prepared isolated adapter:', DEST)
