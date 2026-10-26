"""Shared paths for the full-workbook LUT timing refresh."""
from pathlib import Path
import fill_work9_ours as workbook

ROOT=Path(__file__).resolve().parents[1]
STUDY=ROOT/'experiments/work9-ours-lut-20261015'


def configure():
    workbook.STUDY=STUDY
    workbook.TEMPLATE=STUDY/'work9_before_lut.xlsx'
    workbook.OUTPUT=STUDY/'work9_completed_lut.xlsx'
    return workbook
