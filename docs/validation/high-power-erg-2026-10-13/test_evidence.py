"""Small evidence-only unit checks. These do not run or alter any simulator."""
import unittest
from pathlib import Path
from build_evidence import aggregate, load_all, pair_rows, phases


class EvidenceTests(unittest.TestCase):
    def test_nulls_remain_in_denominator(self):
        rows=[]
        for final, goal in [(None,False),(9.98,True),(10.0,False)]:
            rows.append(dict(cohort='short',signal='effective_plant',role='sprint',sprint_target_w=500,
                variant='candidate',final_tail_s=final,goal_met=goal,overshoot_w=11,peak_w=511,mae_w=12))
        row=next(r for r in aggregate(rows) if r['sprint_target_w']=='all')
        self.assertEqual((row['attempts'],row['goal_met'],row['final_tail_found'],row['null_final_tail']),(3,1,2,1))
        self.assertEqual(row['overshoot_above_10w'],3)

    def test_incomplete_config_is_refused_before_io(self):
        with self.assertRaisesRegex(ValueError,'incomplete'):
            load_all(Path('/nonexistent'),{'status':'incomplete'})

    def test_partial_matrix_is_refused(self):
        with self.assertRaisesRegex(ValueError,'four-cohort'):
            load_all(Path('/nonexistent'),{'status':'ready_for_evidence','cohorts':[]})

    def test_long_phases_not_double_counted(self):
        value={'phases':[{'name':'early_sprint','role':'sprint','response':{'example':1}}],
            'sprints':[{'name':'early_sprint','role':'sprint','response':{'example':1}}]}
        self.assertEqual(len(list(phases(value))),1)

    def test_pair_requires_both_variants(self):
        value=dict(cohort='short',case='test',signal='delivered_sensor',phase='sprint',variant='candidate')
        with self.assertRaises(AssertionError):pair_rows([value])


if __name__=='__main__':unittest.main()
