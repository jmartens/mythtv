/*
 * Error resilience / concealment
 *
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

/**
 * replaces the current MB with a flat dc only version.
 */
static void put_dc(MpegEncContext *s, uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr, int mb_x, int mb_y)
{
    int dc, dcu, dcv, y, i;
    for(i=0; i<4; i++){
        dc= s->dc_val[0][mb_x*2+1 + (i&1) + (mb_y*2+1 + (i>>1))*(s->mb_width*2+2)];
        if(dc<0) dc=0;
        else if(dc>2040) dc=2040;
        for(y=0; y<8; y++){
            int x;
            for(x=0; x<8; x++){
                dest_y[x + (i&1)*8 + (y + (i>>1)*8)*s->linesize]= dc/8;
            }
        }
    }
    dcu = s->dc_val[1][mb_x+1 + (mb_y+1)*(s->mb_width+2)];
    dcv = s->dc_val[2][mb_x+1 + (mb_y+1)*(s->mb_width+2)];
    if     (dcu<0   ) dcu=0;
    else if(dcu>2040) dcu=2040;
    if     (dcv<0   ) dcv=0;
    else if(dcv>2040) dcv=2040;
    for(y=0; y<8; y++){
        int x;
        for(x=0; x<8; x++){
            dest_cb[x + y*(s->uvlinesize)]= dcu/8;
            dest_cr[x + y*(s->uvlinesize)]= dcv/8;
        }
    }
}

static void filter181(INT16 *data, int width, int height, int stride){
    int x,y;

    /* horizontal filter */
    for(y=1; y<height-1; y++){
        int prev_dc= data[0 + y*stride];

        for(x=1; x<width-1; x++){
            int dc;
            
            dc= - prev_dc 
                + data[x     + y*stride]*8
                - data[x + 1 + y*stride];
            dc= (dc*10923 + 32768)>>16;
            prev_dc= data[x + y*stride];
            data[x + y*stride]= dc;
        }
    }
    
    /* vertical filter */
    for(x=1; x<width-1; x++){
        int prev_dc= data[x];

        for(y=1; y<height-1; y++){
            int dc;
            
            dc= - prev_dc 
                + data[x +  y   *stride]*8
                - data[x + (y+1)*stride];
            dc= (dc*10923 + 32768)>>16;
            prev_dc= data[x + y*stride];
            data[x + y*stride]= dc;
        }
    }
}

/**
 * guess the dc of blocks which dont have a undamaged dc
 * @param w	width in 8 pixel blocks
 * @param h	height in 8 pixel blocks
 */
static void guess_dc(MpegEncContext *s, INT16 *dc, int w, int h, int stride, int is_luma){
    int b_x, b_y;

    for(b_y=0; b_y<h; b_y++){
        for(b_x=0; b_x<w; b_x++){
            int color[4]={1024,1024,1024,1024};
            int distance[4]={9999,9999,9999,9999};
            int mb_index, error, j;
            INT64 guess, weight_sum;
            
            mb_index= (b_x>>is_luma) + (b_y>>is_luma)*s->mb_width;
            
            error= s->error_status_table[mb_index];
            
            if(!(s->mb_type[mb_index]&MB_TYPE_INTRA)) continue; //inter
            if(!(error&DC_ERROR)) continue;           //dc-ok
            
            /* right block */
            for(j=b_x+1; j<w; j++){
                int mb_index_j= (j>>is_luma) + (b_y>>is_luma)*s->mb_width;
                int error_j= s->error_status_table[mb_index_j];
                int intra_j= s->mb_type[mb_index_j]&MB_TYPE_INTRA;
                if(intra_j==0 || !(error_j&DC_ERROR)){
                    color[0]= dc[j + b_y*stride];
                    distance[0]= j-b_x;
                    break;
                }
            }
            
            /* left block */
            for(j=b_x-1; j>=0; j--){
                int mb_index_j= (j>>is_luma) + (b_y>>is_luma)*s->mb_width;
                int error_j= s->error_status_table[mb_index_j];
                int intra_j= s->mb_type[mb_index_j]&MB_TYPE_INTRA;
                if(intra_j==0 || !(error_j&DC_ERROR)){
                    color[1]= dc[j + b_y*stride];
                    distance[1]= b_x-j;
                    break;
                }
            }

            /* bottom block */
            for(j=b_y+1; j<h; j++){
                int mb_index_j= (b_x>>is_luma) + (j>>is_luma)*s->mb_width;
                int error_j= s->error_status_table[mb_index_j];
                int intra_j= s->mb_type[mb_index_j]&MB_TYPE_INTRA;
                if(intra_j==0 || !(error_j&DC_ERROR)){
                    color[2]= dc[b_x + j*stride];
                    distance[2]= j-b_y;
                    break;
                }
            }

            /* top block */
            for(j=b_y-1; j>=0; j--){
                int mb_index_j= (b_x>>is_luma) + (j>>is_luma)*s->mb_width;
                int error_j= s->error_status_table[mb_index_j];
                int intra_j= s->mb_type[mb_index_j]&MB_TYPE_INTRA;
                if(intra_j==0 || !(error_j&DC_ERROR)){
                    color[3]= dc[b_x + j*stride];
                    distance[3]= b_y-j;
                    break;
                }
            }
            
            weight_sum=0;
            guess=0;
            for(j=0; j<4; j++){
                INT64 weight= 256*256*256*16/distance[j];
                guess+= weight*(INT64)color[j];
                weight_sum+= weight;
            }
            guess= (guess + weight_sum/2) / weight_sum;

            dc[b_x + b_y*stride]= guess;
        }
    }
}

/**
 * simple horizontal deblocking filter used for error resilience
 * @param w	width in 8 pixel blocks
 * @param h	height in 8 pixel blocks
 */
static void h_block_filter(MpegEncContext *s, UINT8 *dst, int w, int h, int stride, int is_luma){
    int b_x, b_y;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;

    for(b_y=0; b_y<h; b_y++){
        for(b_x=0; b_x<w-1; b_x++){
            int y;
            int left_status = s->error_status_table[( b_x   >>is_luma) + (b_y>>is_luma)*s->mb_width];
            int right_status= s->error_status_table[((b_x+1)>>is_luma) + (b_y>>is_luma)*s->mb_width];
            int left_intra=   s->mb_type      [( b_x   >>is_luma) + (b_y>>is_luma)*s->mb_width]&MB_TYPE_INTRA;
            int right_intra=  s->mb_type      [((b_x+1)>>is_luma) + (b_y>>is_luma)*s->mb_width]&MB_TYPE_INTRA;
            int left_damage =  left_status&(DC_ERROR|AC_ERROR|MV_ERROR);
            int right_damage= right_status&(DC_ERROR|AC_ERROR|MV_ERROR);
            int offset= b_x*8 + b_y*stride*8;
            INT16 *left_mv=  s->motion_val[s->block_wrap[0]*((b_y<<(1-is_luma)) + 1) + ( b_x   <<(1-is_luma))];
            INT16 *right_mv= s->motion_val[s->block_wrap[0]*((b_y<<(1-is_luma)) + 1) + ((b_x+1)<<(1-is_luma))];
            
            if(!(left_damage||right_damage)) continue; // both undamaged
            
            if(   (!left_intra) && (!right_intra) 
               && ABS(left_mv[0]-right_mv[0]) + ABS(left_mv[1]+right_mv[1]) < 2) continue;
            
            for(y=0; y<8; y++){
                int a,b,c,d;
                
                a= dst[offset + 7 + y*stride] - dst[offset + 6 + y*stride];
                b= dst[offset + 8 + y*stride] - dst[offset + 7 + y*stride];
                c= dst[offset + 9 + y*stride] - dst[offset + 8 + y*stride];
                
                d= ABS(b) - ((ABS(a) + ABS(c) + 1)>>1);
                d= FFMAX(d, 0);
                if(b<0) d= -d;
                
                if(d==0) continue;

                if(!(left_damage && right_damage))
                    d= d*16/9;
                
                if(left_damage){
                    dst[offset + 7 + y*stride] = cm[dst[offset + 7 + y*stride] + ((d*7)>>4)];
                    dst[offset + 6 + y*stride] = cm[dst[offset + 6 + y*stride] + ((d*5)>>4)];
                    dst[offset + 5 + y*stride] = cm[dst[offset + 5 + y*stride] + ((d*3)>>4)];
                    dst[offset + 4 + y*stride] = cm[dst[offset + 4 + y*stride] + ((d*1)>>4)];
                }
                if(right_damage){
                    dst[offset + 8 + y*stride] = cm[dst[offset + 8 + y*stride] - ((d*7)>>4)];
                    dst[offset + 9 + y*stride] = cm[dst[offset + 9 + y*stride] - ((d*5)>>4)];
                    dst[offset + 10+ y*stride] = cm[dst[offset +10 + y*stride] - ((d*3)>>4)];
                    dst[offset + 11+ y*stride] = cm[dst[offset +11 + y*stride] - ((d*1)>>4)];
                }
            }
        }
    }
}

/**
 * simple vertical deblocking filter used for error resilience
 * @param w	width in 8 pixel blocks
 * @param h	height in 8 pixel blocks
 */
static void v_block_filter(MpegEncContext *s, UINT8 *dst, int w, int h, int stride, int is_luma){
    int b_x, b_y;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;

    for(b_y=0; b_y<h-1; b_y++){
        for(b_x=0; b_x<w; b_x++){
            int x;
            int top_status   = s->error_status_table[(b_x>>is_luma) + ( b_y   >>is_luma)*s->mb_width];
            int bottom_status= s->error_status_table[(b_x>>is_luma) + ((b_y+1)>>is_luma)*s->mb_width];
            int top_intra=     s->mb_type      [(b_x>>is_luma) + ( b_y   >>is_luma)*s->mb_width]&MB_TYPE_INTRA;
            int bottom_intra=  s->mb_type      [(b_x>>is_luma) + ((b_y+1)>>is_luma)*s->mb_width]&MB_TYPE_INTRA;
            int top_damage =      top_status&(DC_ERROR|AC_ERROR|MV_ERROR);
            int bottom_damage= bottom_status&(DC_ERROR|AC_ERROR|MV_ERROR);
            int offset= b_x*8 + b_y*stride*8;
            INT16 *top_mv=    s->motion_val[s->block_wrap[0]*(( b_y   <<(1-is_luma)) + 1) + (b_x<<(1-is_luma))];
            INT16 *bottom_mv= s->motion_val[s->block_wrap[0]*(((b_y+1)<<(1-is_luma)) + 1) + (b_x<<(1-is_luma))];
            
            if(!(top_damage||bottom_damage)) continue; // both undamaged
            
            if(   (!top_intra) && (!bottom_intra) 
               && ABS(top_mv[0]-bottom_mv[0]) + ABS(top_mv[1]+bottom_mv[1]) < 2) continue;
            
            for(x=0; x<8; x++){
                int a,b,c,d;
                
                a= dst[offset + x + 7*stride] - dst[offset + x + 6*stride];
                b= dst[offset + x + 8*stride] - dst[offset + x + 7*stride];
                c= dst[offset + x + 9*stride] - dst[offset + x + 8*stride];
                
                d= ABS(b) - ((ABS(a) + ABS(c)+1)>>1);
                d= FFMAX(d, 0);
                if(b<0) d= -d;
                
                if(d==0) continue;

                if(!(top_damage && bottom_damage))
                    d= d*16/9;
                
                if(top_damage){
                    dst[offset + x +  7*stride] = cm[dst[offset + x +  7*stride] + ((d*7)>>4)];
                    dst[offset + x +  6*stride] = cm[dst[offset + x +  6*stride] + ((d*5)>>4)];
                    dst[offset + x +  5*stride] = cm[dst[offset + x +  5*stride] + ((d*3)>>4)];
                    dst[offset + x +  4*stride] = cm[dst[offset + x +  4*stride] + ((d*1)>>4)];
                }
                if(bottom_damage){
                    dst[offset + x +  8*stride] = cm[dst[offset + x +  8*stride] - ((d*7)>>4)];
                    dst[offset + x +  9*stride] = cm[dst[offset + x +  9*stride] - ((d*5)>>4)];
                    dst[offset + x + 10*stride] = cm[dst[offset + x + 10*stride] - ((d*3)>>4)];
                    dst[offset + x + 11*stride] = cm[dst[offset + x + 11*stride] - ((d*1)>>4)];
                }
            }
        }
    }
}

static void guess_mv(MpegEncContext *s){
    UINT8 fixed[s->mb_num];
#define MV_FROZEN    3
#define MV_CHANGED   2
#define MV_UNCHANGED 1
    const int mb_width = s->mb_width;
    const int mb_height= s->mb_height;
    int i, depth, num_avail;
   
    num_avail=0;
    for(i=0; i<s->mb_num; i++){
        int f=0;
        int error= s->error_status_table[i];

        if(s->mb_type[i]&MB_TYPE_INTRA) f=MV_FROZEN; //intra //FIXME check
        if(!(error&MV_ERROR)) f=MV_FROZEN;           //inter with undamaged MV
        
        fixed[i]= f;
        if(f==MV_FROZEN)
            num_avail++;
    }
    
    if((!(s->avctx->error_concealment&FF_EC_GUESS_MVS)) || num_avail <= mb_width/2){
        int mb_x, mb_y;
        i= -1;
        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            for(mb_x=0; mb_x<s->mb_width; mb_x++){
                i++;
                
                if(s->mb_type[i]&MB_TYPE_INTRA) continue;
                if(!(s->error_status_table[i]&MV_ERROR)) continue;

                s->mv_dir = MV_DIR_FORWARD;
                s->mb_intra=0;
                s->mv_type = MV_TYPE_16X16;
                s->mb_skiped=0;

		s->dsp.clear_blocks(s->block[0]);

                s->mb_x= mb_x;
                s->mb_y= mb_y;
                s->mv[0][0][0]= 0;
                s->mv[0][0][1]= 0;
                MPV_decode_mb(s, s->block);
            }
        }
        return;
    }
    
    for(depth=0;; depth++){
        int changed, pass, none_left;

        none_left=1;
        changed=1;
        for(pass=0; (changed || pass<2) && pass<10; pass++){
            int i,mb_x, mb_y;
int score_sum=0;
 
            changed=0;
            i= -1;
            for(mb_y=0; mb_y<s->mb_height; mb_y++){
                for(mb_x=0; mb_x<s->mb_width; mb_x++){
                    int mv_predictor[8][2]={{0}};
                    int pred_count=0;
                    int j;
                    int best_score=256*256*256*64;
                    int best_pred=0;
                    const int mot_stride= mb_width*2+2;
                    const int mot_index= mb_x*2 + 1 + (mb_y*2+1)*mot_stride;
                    int prev_x= s->motion_val[mot_index][0];
                    int prev_y= s->motion_val[mot_index][1];

                    i++;
                    if((mb_x^mb_y^pass)&1) continue;
                    
                    if(fixed[i]==MV_FROZEN) continue;
                    
                    j=0;
                    if(mb_x>0           && fixed[i-1       ]==MV_FROZEN) j=1;
                    if(mb_x+1<mb_width  && fixed[i+1       ]==MV_FROZEN) j=1;
                    if(mb_y>0           && fixed[i-mb_width]==MV_FROZEN) j=1;
                    if(mb_y+1<mb_height && fixed[i+mb_width]==MV_FROZEN) j=1;
                    if(j==0) continue;

                    j=0;
                    if(mb_x>0           && fixed[i-1       ]==MV_CHANGED) j=1;
                    if(mb_x+1<mb_width  && fixed[i+1       ]==MV_CHANGED) j=1;
                    if(mb_y>0           && fixed[i-mb_width]==MV_CHANGED) j=1;
                    if(mb_y+1<mb_height && fixed[i+mb_width]==MV_CHANGED) j=1;
                    if(j==0 && pass>1) continue;
                    
                    none_left=0;
                    
                    if(mb_x>0 && fixed[i-1]){
                        mv_predictor[pred_count][0]= s->motion_val[mot_index - 2][0];
                        mv_predictor[pred_count][1]= s->motion_val[mot_index - 2][1];
                        pred_count++;
                    }
                    if(mb_x+1<mb_width && fixed[i+1]){
                        mv_predictor[pred_count][0]= s->motion_val[mot_index + 2][0];
                        mv_predictor[pred_count][1]= s->motion_val[mot_index + 2][1];
                        pred_count++;
                    }
                    if(mb_y>0 && fixed[i-mb_width]){
                        mv_predictor[pred_count][0]= s->motion_val[mot_index - mot_stride*2][0];
                        mv_predictor[pred_count][1]= s->motion_val[mot_index - mot_stride*2][1];
                        pred_count++;
                    }
                    if(mb_y+1<mb_height && fixed[i+mb_width]){
                        mv_predictor[pred_count][0]= s->motion_val[mot_index + mot_stride*2][0];
                        mv_predictor[pred_count][1]= s->motion_val[mot_index + mot_stride*2][1];
                        pred_count++;
                    }
                    if(pred_count==0) continue;
                    
                    if(pred_count>1){
                        int sum_x=0, sum_y=0;
                        int max_x, max_y, min_x, min_y;

                        for(j=0; j<pred_count; j++){
                            sum_x+= mv_predictor[j][0];
                            sum_y+= mv_predictor[j][1];
                        }
                    
                        /* mean */
                        mv_predictor[pred_count][0] = sum_x/j;
                        mv_predictor[pred_count][1] = sum_y/j;
                    
                        /* median */
                        if(pred_count>=3){
                            min_y= min_x= 99999;
                            max_y= max_x=-99999;
                        }else{
                            min_x=min_y=max_x=max_y=0;
                        }
                        for(j=0; j<pred_count; j++){
                            max_x= FFMAX(max_x, mv_predictor[j][0]);
                            max_y= FFMAX(max_y, mv_predictor[j][1]);
                            min_x= FFMIN(min_x, mv_predictor[j][0]);
                            min_y= FFMIN(min_y, mv_predictor[j][1]);
                        }
                        mv_predictor[pred_count+1][0] = sum_x - max_x - min_x;
                        mv_predictor[pred_count+1][1] = sum_y - max_y - min_y;
                        
                        if(pred_count==4){
                            mv_predictor[pred_count+1][0] /= 2;
                            mv_predictor[pred_count+1][1] /= 2;
                        }
                        pred_count+=2;
                    }
                    
                    /* zero MV */
                    pred_count++;

                    /* last MV */
                    mv_predictor[pred_count][0]= s->motion_val[mot_index][0];
                    mv_predictor[pred_count][1]= s->motion_val[mot_index][1];
                    pred_count++;                    
                    
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra=0;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_skiped=0;

		    s->dsp.clear_blocks(s->block[0]);

                    s->mb_x= mb_x;
                    s->mb_y= mb_y;
                    for(j=0; j<pred_count; j++){
                        int score=0;
                        UINT8 *src= s->current_picture.data[0] + mb_x*16 + mb_y*16*s->linesize;

                        s->motion_val[mot_index][0]= s->mv[0][0][0]= mv_predictor[j][0];
                        s->motion_val[mot_index][1]= s->mv[0][0][1]= mv_predictor[j][1];
                        MPV_decode_mb(s, s->block);
                        
                        if(mb_x>0 && fixed[i-1]){
                            int k;
                            for(k=0; k<16; k++)
                                score += ABS(src[k*s->linesize-1 ]-src[k*s->linesize   ]);
                        }
                        if(mb_x+1<mb_width && fixed[i+1]){
                            int k;
                            for(k=0; k<16; k++)
                                score += ABS(src[k*s->linesize+15]-src[k*s->linesize+16]);
                        }
                        if(mb_y>0 && fixed[i-mb_width]){
                            int k;
                            for(k=0; k<16; k++)
                                score += ABS(src[k-s->linesize   ]-src[k               ]);
                        }
                        if(mb_y+1<mb_height && fixed[i+mb_width]){
                            int k;
                            for(k=0; k<16; k++)
                                score += ABS(src[k+s->linesize*15]-src[k+s->linesize*16]);
                        }
                        
                        if(score <= best_score){ // <= will favor the last MV
                            best_score= score;
                            best_pred= j;
                        }
                    }
score_sum+= best_score;
//FIXME no need to set s->motion_val[mot_index][0] explicit
                    s->motion_val[mot_index][0]= s->mv[0][0][0]= mv_predictor[best_pred][0];
                    s->motion_val[mot_index][1]= s->mv[0][0][1]= mv_predictor[best_pred][1];

                    MPV_decode_mb(s, s->block);

                    
                    if(s->mv[0][0][0] != prev_x || s->mv[0][0][1] != prev_y){
                        fixed[i]=MV_CHANGED;
                        changed++;
                    }else
                        fixed[i]=MV_UNCHANGED;
                }
            }

//            printf(".%d/%d", changed, score_sum); fflush(stdout);
        }
        
        if(none_left) 
            return;
            
        for(i=0; i<s->mb_num; i++){
            if(fixed[i])
                fixed[i]=MV_FROZEN;
        }
//        printf(":"); fflush(stdout);
    }
}
    
static int is_intra_more_likely(MpegEncContext *s){
    int is_intra_likely, i, j, undamaged_count, skip_amount, mb_x, mb_y;
    
    if(s->last_picture.data[0]==NULL) return 1; //no previous frame available -> use spatial prediction

    undamaged_count=0;
    for(i=0; i<s->mb_num; i++){
        int error= s->error_status_table[i];
        if(!((error&DC_ERROR) && (error&MV_ERROR)))
            undamaged_count++;
    }
    
    if(undamaged_count < 5) return 0; //allmost all MBs damaged -> use temporal prediction
    
    skip_amount= FFMAX(undamaged_count/50, 1); //check only upto 50 MBs 
    is_intra_likely=0;

    j=0;
    i=-1;
    for(mb_y= 0; mb_y<s->mb_height-1; mb_y++){
        for(mb_x= 0; mb_x<s->mb_width; mb_x++){
            int error;

            i++;
            error= s->error_status_table[i];
            if((error&DC_ERROR) && (error&MV_ERROR))
                continue; //skip damaged
        
            j++;    
            if((j%skip_amount) != 0) continue; //skip a few to speed things up
    
            if(s->pict_type==I_TYPE){
                UINT8 *mb_ptr     = s->current_picture.data[0] + mb_x*16 + mb_y*16*s->linesize;
                UINT8 *last_mb_ptr= s->last_picture.data   [0] + mb_x*16 + mb_y*16*s->linesize;
    
		is_intra_likely += s->dsp.pix_abs16x16(last_mb_ptr, mb_ptr                    , s->linesize);
                is_intra_likely -= s->dsp.pix_abs16x16(last_mb_ptr, last_mb_ptr+s->linesize*16, s->linesize);
            }else{
                if(s->mbintra_table[i]) //HACK (this is allways inited but we should use mb_type[])
                   is_intra_likely++;
                else
                   is_intra_likely--;
            }
        }
    }
//printf("is_intra_likely: %d type:%d\n", is_intra_likely, s->pict_type);
    return is_intra_likely > 0;    
}

void ff_error_resilience(MpegEncContext *s){
    int i, mb_x, mb_y, error, error_type;
    int distance;
    int threshold_part[4]= {100,100,100};
    int threshold= 50;
    int is_intra_likely;
    
#if 1
    /* handle overlapping slices */
    for(error_type=1; error_type<=3; error_type++){
        int end_ok=0;

        for(i=s->mb_num-1; i>=0; i--){
            int error= s->error_status_table[i];
        
            if(error&(1<<error_type))
                end_ok=1;
            if(error&(8<<error_type))
                end_ok=1;

            if(!end_ok)
                s->error_status_table[i]|= 1<<error_type;

            if(error&VP_START)
                end_ok=0;
        }
    }
#endif
#if 1
    /* handle slices with partitions of different length */
    if(s->partitioned_frame){
        int end_ok=0;

        for(i=s->mb_num-1; i>=0; i--){
            int error= s->error_status_table[i];
        
            if(error&AC_END)
                end_ok=0;
            if((error&MV_END) || (error&DC_END) || (error&AC_ERROR))
                end_ok=1;

            if(!end_ok)
                s->error_status_table[i]|= AC_ERROR;

            if(error&VP_START)
                end_ok=0;
        }
    }
#endif
    /* handle missing slices */
    if(s->error_resilience>=4){
        int end_ok=1;
                
        for(i=s->mb_num-2; i>=s->mb_width+100; i--){ //FIXME +100 hack
            int error1= s->error_status_table[i  ];
            int error2= s->error_status_table[i+1];
        
            if(error1&VP_START)
                end_ok=1;
             
            if(   error2==(VP_START|DC_ERROR|AC_ERROR|MV_ERROR|AC_END|DC_END|MV_END)
               && error1!=(VP_START|DC_ERROR|AC_ERROR|MV_ERROR|AC_END|DC_END|MV_END) 
               && ((error1&AC_END) || (error1&DC_END) || (error1&MV_END))){ //end & uninited
                end_ok=0;
            }
        
            if(!end_ok)
                s->error_status_table[i]|= DC_ERROR|AC_ERROR|MV_ERROR;
        }
    }
    
#if 1
    /* backward mark errors */
    distance=9999999;
    for(error_type=1; error_type<=3; error_type++){
        for(i=s->mb_num-1; i>=0; i--){
            int error= s->error_status_table[i];
            
            if(!s->mbskip_table[i]) //FIXME partition specific
                distance++;            
            if(error&(1<<error_type))
                distance= 0;

            if(s->partitioned_frame){
                if(distance < threshold_part[error_type-1])
                    s->error_status_table[i]|= 1<<error_type;
            }else{
                if(distance < threshold)
                    s->error_status_table[i]|= 1<<error_type;
            }

            if(error&VP_START)
                distance= 9999999;
        }
    }
#endif

    /* forward mark errors */
    error=0;
    for(i=0; i<s->mb_num; i++){
        int old_error= s->error_status_table[i];
        
        if(old_error&VP_START)
            error= old_error& (DC_ERROR|AC_ERROR|MV_ERROR);
        else{
            error|= old_error& (DC_ERROR|AC_ERROR|MV_ERROR);
            s->error_status_table[i]|= error;
        }
    }
#if 1
    /* handle not partitioned case */
    if(!s->partitioned_frame){
        for(i=0; i<s->mb_num; i++){
            error= s->error_status_table[i];
            if(error&(AC_ERROR|DC_ERROR|MV_ERROR))
                error|= AC_ERROR|DC_ERROR|MV_ERROR;
            s->error_status_table[i]= error;
        }
    }
#endif
    is_intra_likely= is_intra_more_likely(s);

    /* set unknown mb-type to most likely */
    for(i=0; i<s->mb_num; i++){
        int intra;
        error= s->error_status_table[i];
        if((error&DC_ERROR) && (error&MV_ERROR))
            intra= is_intra_likely;
        else
            intra= s->mbintra_table[i];

        if(intra)
            s->mb_type[i]|= MB_TYPE_INTRA;
        else
            s->mb_type[i]&= ~MB_TYPE_INTRA;
    }
    
    /* handle inter blocks with damaged AC */
    i= -1;
    for(mb_y=0; mb_y<s->mb_height; mb_y++){
        for(mb_x=0; mb_x<s->mb_width; mb_x++){
            i++;
            error= s->error_status_table[i];

            if(s->mb_type[i]&MB_TYPE_INTRA) continue; //intra
            if(error&MV_ERROR) continue;              //inter with damaged MV
            if(!(error&AC_ERROR)) continue;           //undamaged inter
            
            s->mv_dir = MV_DIR_FORWARD;
            s->mb_intra=0;
            s->mb_skiped=0;
            if(s->mb_type[i]&MB_TYPE_INTER4V){
                int mb_index= mb_x*2+1 + (mb_y*2+1)*s->block_wrap[0];
                int j;
                s->mv_type = MV_TYPE_8X8;
                for(j=0; j<4; j++){
                    s->mv[0][j][0] = s->motion_val[ mb_index + (j&1) + (j>>1)*s->block_wrap[0] ][0];
                    s->mv[0][j][1] = s->motion_val[ mb_index + (j&1) + (j>>1)*s->block_wrap[0] ][1];
                }
            }else{
                s->mv_type = MV_TYPE_16X16;
                s->mv[0][0][0] = s->motion_val[ mb_x*2+1 + (mb_y*2+1)*s->block_wrap[0] ][0];
                s->mv[0][0][1] = s->motion_val[ mb_x*2+1 + (mb_y*2+1)*s->block_wrap[0] ][1];
            }
        
	    s->dsp.clear_blocks(s->block[0]);

            s->mb_x= mb_x;
            s->mb_y= mb_y;
            MPV_decode_mb(s, s->block);
        }
    }

    /* guess MVs */
    if(s->pict_type==B_TYPE){
        i= -1;
        for(mb_y=0; mb_y<s->mb_height; mb_y++){
            for(mb_x=0; mb_x<s->mb_width; mb_x++){
                int xy= mb_x*2+1 + (mb_y*2+1)*s->block_wrap[0];
                i++;
                error= s->error_status_table[i];

                if(s->mb_type[i]&MB_TYPE_INTRA) continue; //intra
                if(!(error&MV_ERROR)) continue;           //inter with undamaged MV
                if(!(error&AC_ERROR)) continue;           //undamaged inter
            
                s->mv_dir = MV_DIR_FORWARD|MV_DIR_BACKWARD;
                s->mb_intra=0;
                s->mv_type = MV_TYPE_16X16;
                s->mb_skiped=0;
                
                if(s->pp_time){
                    int time_pp= s->pp_time;
                    int time_pb= s->pb_time;
            
                    s->mv[0][0][0] = s->motion_val[xy][0]*time_pb/time_pp;
                    s->mv[0][0][1] = s->motion_val[xy][1]*time_pb/time_pp;
                    s->mv[1][0][0] = s->motion_val[xy][0]*(time_pb - time_pp)/time_pp;
                    s->mv[1][0][1] = s->motion_val[xy][1]*(time_pb - time_pp)/time_pp;
                }else{
                    s->mv[0][0][0]= 0;
                    s->mv[0][0][1]= 0;
                    s->mv[1][0][0]= 0;
                    s->mv[1][0][1]= 0;
                }

                s->dsp.clear_blocks(s->block[0]);
                s->mb_x= mb_x;
                s->mb_y= mb_y;
                MPV_decode_mb(s, s->block);
            }
        }
    }else
        guess_mv(s);

    /* fill DC for inter blocks */
    i= -1;
    for(mb_y=0; mb_y<s->mb_height; mb_y++){
        for(mb_x=0; mb_x<s->mb_width; mb_x++){
            int dc, dcu, dcv, y, n;
            INT16 *dc_ptr;
            UINT8 *dest_y, *dest_cb, *dest_cr;
           
            i++;
            error= s->error_status_table[i];

            if(s->mb_type[i]&MB_TYPE_INTRA) continue; //intra
//            if(error&MV_ERROR) continue; //inter data damaged FIXME is this good?
            
            dest_y = s->current_picture.data[0] + mb_x*16 + mb_y*16*s->linesize;
            dest_cb= s->current_picture.data[1] + mb_x*8  + mb_y*8 *s->uvlinesize;
            dest_cr= s->current_picture.data[2] + mb_x*8  + mb_y*8 *s->uvlinesize;
           
            dc_ptr= &s->dc_val[0][mb_x*2+1 + (mb_y*2+1)*(s->mb_width*2+2)];
            for(n=0; n<4; n++){
                dc=0;
                for(y=0; y<8; y++){
                    int x;
                    for(x=0; x<8; x++){
                       dc+= dest_y[x + (n&1)*8 + (y + (n>>1)*8)*s->linesize];
                    }
                }
                dc_ptr[(n&1) + (n>>1)*(s->mb_width*2+2)]= (dc+4)>>3;
            }

            dcu=dcv=0;
            for(y=0; y<8; y++){
                int x;
                for(x=0; x<8; x++){
                    dcu+=dest_cb[x + y*(s->uvlinesize)];
                    dcv+=dest_cr[x + y*(s->uvlinesize)];
                }
            }
            s->dc_val[1][mb_x+1 + (mb_y+1)*(s->mb_width+2)]= (dcu+4)>>3;
            s->dc_val[2][mb_x+1 + (mb_y+1)*(s->mb_width+2)]= (dcv+4)>>3;   
        }
    }
#if 1
    /* guess DC for damaged blocks */
    guess_dc(s, s->dc_val[0] + s->mb_width*2+3, s->mb_width*2, s->mb_height*2, s->mb_width*2+2, 1);
    guess_dc(s, s->dc_val[1] + s->mb_width  +3, s->mb_width  , s->mb_height  , s->mb_width  +2, 0);
    guess_dc(s, s->dc_val[2] + s->mb_width  +3, s->mb_width  , s->mb_height  , s->mb_width  +2, 0);
#endif   
    /* filter luma DC */
    filter181(s->dc_val[0] + s->mb_width*2+3, s->mb_width*2, s->mb_height*2, s->mb_width*2+2);
    
#if 1
    /* render DC only intra */
    i= -1;
    for(mb_y=0; mb_y<s->mb_height; mb_y++){
        for(mb_x=0; mb_x<s->mb_width; mb_x++){
            UINT8 *dest_y, *dest_cb, *dest_cr;
           
            i++;
            error= s->error_status_table[i];

            if(!(s->mb_type[i]&MB_TYPE_INTRA)) continue; //inter
            if(!(error&AC_ERROR)) continue;              //undamaged
            
            dest_y = s->current_picture.data[0] + mb_x*16 + mb_y*16*s->linesize;
            dest_cb= s->current_picture.data[1] + mb_x*8  + mb_y*8 *s->uvlinesize;
            dest_cr= s->current_picture.data[2] + mb_x*8  + mb_y*8 *s->uvlinesize;
            
            put_dc(s, dest_y, dest_cb, dest_cr, mb_x, mb_y);
        }
    }
#endif
    
    if(s->avctx->error_concealment&FF_EC_DEBLOCK){
        /* filter horizontal block boundaries */
        h_block_filter(s, s->current_picture.data[0], s->mb_width*2, s->mb_height*2, s->linesize  , 1);
        h_block_filter(s, s->current_picture.data[1], s->mb_width  , s->mb_height  , s->uvlinesize, 0);
        h_block_filter(s, s->current_picture.data[2], s->mb_width  , s->mb_height  , s->uvlinesize, 0);

        /* filter vertical block boundaries */
        v_block_filter(s, s->current_picture.data[0], s->mb_width*2, s->mb_height*2, s->linesize  , 1);
        v_block_filter(s, s->current_picture.data[1], s->mb_width  , s->mb_height  , s->uvlinesize, 0);
        v_block_filter(s, s->current_picture.data[2], s->mb_width  , s->mb_height  , s->uvlinesize, 0);
    }

    /* clean a few tables */
    for(i=0; i<s->mb_num; i++){
        int error= s->error_status_table[i];
        
        if(s->pict_type!=B_TYPE && (error&(DC_ERROR|MV_ERROR|AC_ERROR))){
            s->mbskip_table[i]=0;
        }
        s->mbintra_table[i]=1;
    }    
}
