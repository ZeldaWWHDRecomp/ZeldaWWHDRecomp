/* Public CC0 daKokiie_Execute021A57A0: complete movement/matrix/camera consumer.
 * MIT randomizer flexible_item_locations.asm021A5940 removes only the branch
 * after its retained boomerang query, allowing the cut flower to fall without
 * owning that item. No shared ownership override or caller scope is used. */
#pragma once
#include "flower_helpers.h"
#include "flower_movement.h"
#include "flower_camera.h"
WWHD_REPLACE(0x021A57A0,s32,randomizer_flower_execute,(u32 a)){
    (void)wwhd_dComIfG_getGameInfo();(void)wwhd_dComIfG_getGameInfo();
    randomizer_flower_move(a);
    rf_translate(RFF(a,0x314),RFF(a,0x318),RFF(a,0x31C),0);
    rf_y_rotate(rf_matrix(),RFS16(a,0x32A));
    rf_y_rotate(rf_matrix(),RFS16(a,0x44E));
    rf_x_rotate(rf_matrix(),RFS16(a,0x450));
    rf_y_rotate(rf_matrix(),(s16)(u16)-(s32)RFS16(a,0x44E));
    rf_x_rotate(rf_matrix(),RFS16(a,0x328));
    rf_z_rotate(rf_matrix(),RFS16(a,0x32C));
    rf_y_rotate(rf_matrix(),0xB54);
    const u32 matrix=rf_matrix(),model=RFP(a,0x3B8);
    rf_model_matrix(matrix,model);
    rf_matrix_copy(rf_matrix(),a+0x45C);
    rf_background_move(RFP(a,0x48C));
    rf_y_rotate(rf_matrix(),-0xB54);
    for(s32 i=0;i<5;i++){
        rf_push();rf_y_rotate(rf_matrix(),(s16)(u16)(i*0x3333));
        randomizer_flower_vector point={0,rf_reg_f(0,5)-170.0f,rf_reg_f(0,6)+400.0f};
        rf_position(&point,a+0x40C+12*(u32)i);rf_pull();
    }
    randomizer_flower_camera(a);
    return 1;
}
