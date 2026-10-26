use plonky2::field::goldilocks_field::GoldilocksField;
use plonky2::plonk::circuit_data::VerifierCircuitData;
use plonky2::plonk::config::PoseidonGoldilocksConfig;
use plonky2::util::serialization::DefaultGateSerializer;
fn main() {
    if std::env::args().nth(1).as_deref() == Some("--hash-bench") {
        use plonky2::field::types::Field;
        use plonky2::hash::poseidon::PoseidonHash;
        use plonky2::plonk::config::Hasher;
        for n in [8192usize, 65536, 1048576] {
            let values: Vec<_> = (0..n).map(|i| GoldilocksField::from_canonical_usize(i)).collect();
            let mut times = Vec::new();
            for _ in 0..5 {
                let t = std::time::Instant::now();
                std::hint::black_box(PoseidonHash::hash_no_pad(&values));
                times.push(t.elapsed().as_secs_f64() * 1000.0);
            }
            times.sort_by(f64::total_cmp);
            println!("{}", serde_json::json!({"role":"native public-input hash microbenchmark; not proof verification","fields":n,"median_ms":times[2]}));
        }
        return;
    }
    for path in std::env::args().skip(1) {
        let bytes = std::fs::read(&path).unwrap();
        let v = VerifierCircuitData::<GoldilocksField, PoseidonGoldilocksConfig, 2>::from_bytes(bytes, &DefaultGateSerializer).unwrap();
        println!("{}", serde_json::json!({"path":path,"degree_bits":v.common.degree_bits(),"public_inputs":v.common.num_public_inputs}));
    }
}
