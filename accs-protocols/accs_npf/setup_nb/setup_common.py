# TODO: Formalize interface for mixins

from typing import Any, Dict


PLATE_TYPES = [
    "corning_96_wellplate_360ul_flat",
    "cellview_96_wellplate_440ul",
    ]
SINGLE_DEST_VOL = 200.
DUPE_DEST_VOL = 150.


class CommonSetupFormItems:
    def init_items(self):
        self.add_item(
            "Operator name",
            config_var='logging.run_info.operator',
            type_=self.ItemType.STRING
        )
        self.add_item(
            "Source plate ID",
            config_var='logging.run_info.source_plate_id',
            type_=self.ItemType.STRING
        )
        self.add_item(
            "Comment",
            config_var='logging.run_info.comment',
            type_=self.ItemType.STRING
        )


class ColRangeSetupFormItems:
    def init_items(self):
        self.add_item(
            "Start column",
            config_var='cell_splitting.start_col',
            type_=self.ItemType.INT,
            min_=1,
            max_=12,
            default=1
        )
        self.add_item(
            "End column",
            config_var='cell_splitting.end_col',
            type_=self.ItemType.INT,
            min_=1,
            max_=12,
            default=12
        )


class SplittingCommonSetupFormItems(CommonSetupFormItems,
                                    ColRangeSetupFormItems):
    def init_items(self):
        CommonSetupFormItems.init_items(self)
        ColRangeSetupFormItems.init_items(self)

        self.add_item(
            "Main plate (slot 2) labware type",
            config_var='cell_splitting.dest_plate_defs.stock_plate',
            type_=self.ItemType.STRING,
            choices=PLATE_TYPES
        )
        self.add_item(
            "Main plate ID/info",
            config_var='logging.run_info.stock_plate_id',
            type_=self.ItemType.STRING
        )
        self.add_item(
            "Duplicate to second plate on slot 1",
            temp_var='two_dest_plates',
            type_=self.ItemType.BOOL,
            default=False
            )
        self.add_item(
            "Second plate (slot 1) labware type",
            temp_var='second_plate_type',
            type_=self.ItemType.STRING,
            choices=PLATE_TYPES
        )
        self.add_item(
            "Second plate ID/info",
            config_var='logging.run_info.dupe_plate_id',
            type_=self.ItemType.STRING
        )
        self.add_item(
            "Fill ratio (main plate to second plate)",
            temp_var='dupe_split_ratio',
            type_=self.ItemType.FLOAT,
            default=1.,
            min_=1./3.,
            max_=8.)

    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        config_vars['cell_splitting.dest_plates'] = ['stock_plate']
        if temp_vars['two_dest_plates']:
            config_vars['cell_splitting.dest_plate_defs.dupe_plate'] \
                = temp_vars['second_plate_type']
            config_vars['cell_splitting.dest_plates'].append('dupe_plate')
            total_vol = 2. * DUPE_DEST_VOL
            dupe_vol = total_vol / (1. + temp_vars['dupe_split_ratio'])
            stock_vol = total_vol - dupe_vol
            greater_vol = max(stock_vol, dupe_vol)
            if greater_vol > SINGLE_DEST_VOL:
                c = SINGLE_DEST_VOL / greater_vol
                dupe_vol *= c
                stock_vol *= c
            config_vars['cell_splitting.dest_target_vols'] = \
                {'stock_plate': stock_vol, 'dupe_plate': dupe_vol}
        else:
            config_vars['cell_splitting.dest_target_vols'] = \
                {'stock_plate': SINGLE_DEST_VOL}


class CherryskippingSetupFormItems:
    def init_items(self):
        self.add_item(
            "Enable \"cherryskipping mode\" and generate a tip-culling script",
            config_var='cell_splitting.cherryskipping_mode',
            type_=self.ItemType.BOOL,
            )

