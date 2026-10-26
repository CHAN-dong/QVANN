import argparse
from work9_lut_study import STUDY,configure

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--update-original',action='store_true')
    update=parser.parse_args().update_original and not (STUDY/'save_copy_only.flag').exists()
    configure().main(update)
