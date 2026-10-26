from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from paper_benchmark_runner import main
if __name__ == '__main__':
    main('VLAH-25TrustCom')
