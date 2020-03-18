#include "state.h"
#include "movegen.h"
#include "eval.h"
#include <stdio.h>

void STATE_reset(chess_state_t *s)
{
    int color;
    int piece;
    for(color = WHITE; color <= BLACK; color++) {
        s->bitboard[color*NUM_TYPES + ALL] = 0;
        for(piece = PAWN; piece <= KING; piece++) {
            s->bitboard[color*NUM_TYPES + piece] = bitboard_start_position[color][piece];
            s->bitboard[color*NUM_TYPES + ALL] |= bitboard_start_position[color][piece];
        }
    }
    s->bitboard[OCCUPIED] = s->bitboard[WHITE_PIECES + ALL] | s->bitboard[BLACK_PIECES + ALL];
    
    s->castling[WHITE] = STATE_FLAGS_QUEEN_CASTLE_POSSIBLE_MASK | STATE_FLAGS_KING_CASTLE_POSSIBLE_MASK;
    s->castling[BLACK] = STATE_FLAGS_QUEEN_CASTLE_POSSIBLE_MASK | STATE_FLAGS_KING_CASTLE_POSSIBLE_MASK;
    s->ep_file = STATE_EN_PASSANT_NONE;
    
    s->player = WHITE;
    
    STATE_compute_hash(s);
    
    s->last_move = 0;
    s->halfmove_clock = 0;
}

static void STATE_add_move_to_list(int pos_to, int pos_from, int type, int captured_type, int special, move_t *moves)
{
    *moves = (pos_from) | (pos_to << 6) | (type << 12) | (captured_type << 15) | (special << 18);
}

static int STATE_add_move_to_list_promotion(int pos_to, int pos_from, move_t *moves)
{
    move_t move = (pos_from) | (pos_to << 6) | (PAWN << 12);
    moves[0] = move | (MOVE_QUEEN_PROMOTION << 18);
    moves[1] = move | (MOVE_ROOK_PROMOTION << 18);
    moves[2] = move | (MOVE_BISHOP_PROMOTION << 18);
    moves[3] = move | (MOVE_KNIGHT_PROMOTION << 18);
    return 4;
}

static int STATE_add_move_to_list_promotion_capture(int pos_to, int pos_from, int captured_type, move_t *moves)
{
    move_t move = (pos_from) | (pos_to << 6) | (PAWN << 12) | (captured_type << 15);
    moves[0] = move | (MOVE_QUEEN_PROMOTION_CAPTURE << 18);
    moves[1] = move | (MOVE_ROOK_PROMOTION_CAPTURE << 18);
    moves[2] = move | (MOVE_BISHOP_PROMOTION_CAPTURE << 18);
    moves[3] = move | (MOVE_KNIGHT_PROMOTION_CAPTURE << 18);
    return 4;
}

int STATE_generate_moves(const chess_state_t *s, move_t *moves)
{
    int num_moves = 0;
    const int player = s->player;
    const int opponent = player ^ 1;
    const int player_index = NUM_TYPES*player;
    const int opponent_index = NUM_TYPES*opponent;
    const bitboard_t player_pieces = s->bitboard[player_index + ALL];
    const bitboard_t opponent_pieces = s->bitboard[opponent_index + ALL];
    int capture_type[64];

    /* Lookup table for opponent type */
    for(int opponent_type = PAWN; opponent_type < KING; opponent_type++) {
        bitboard_t pieces = s->bitboard[opponent_index + opponent_type];
        while(pieces) {
            int pos = BITBOARD_find_bit(pieces);
            capture_type[pos] = opponent_type;
            pieces ^= BITBOARD_POSITION(pos);
        }
    }

    /* Pawns */
    {
        bitboard_t pieces = s->bitboard[player_index + PAWN];
        bitboard_t possible_moves, possible_captures;
        bitboard_t pawn_push2, pawn_captures_from_left, pawn_captures_from_right;
        bitboard_t pawn_promotion, pawn_promotion_captures_from_left, pawn_promotion_captures_from_right;
        MOVEGEN_all_pawns(player, pieces, player_pieces, opponent_pieces, &possible_moves, &pawn_push2, &pawn_captures_from_left, &pawn_captures_from_right, &pawn_promotion, &pawn_promotion_captures_from_left, &pawn_promotion_captures_from_right);

        int attack_from_left, attack_from_right, step;

        if(player == WHITE) {
            attack_from_left = -9;
            attack_from_right = -7;
            step = -8;
        } else {
            attack_from_left = 7;
            attack_from_right = 9;
            step = 8;
        }

        /* Captures */
        while(pawn_captures_from_left) {
            int pos_to = BITBOARD_find_bit(pawn_captures_from_left);
            STATE_add_move_to_list(pos_to, pos_to + attack_from_left, PAWN, capture_type[pos_to], MOVE_CAPTURE, moves + num_moves++);
            pawn_captures_from_left ^= BITBOARD_POSITION(pos_to);
        }

        while(pawn_captures_from_right) {
            int pos_to = BITBOARD_find_bit(pawn_captures_from_right);
            STATE_add_move_to_list(pos_to, pos_to + attack_from_right, PAWN, capture_type[pos_to], MOVE_CAPTURE, moves + num_moves++);
            pawn_captures_from_right ^= BITBOARD_POSITION(pos_to);
        }

        /* Promotion with capture */
        while(pawn_promotion_captures_from_left) {
            int pos_to = BITBOARD_find_bit(pawn_promotion_captures_from_left);
            num_moves += STATE_add_move_to_list_promotion_capture(pos_to, pos_to + attack_from_left, capture_type[pos_to], moves + num_moves);
            pawn_promotion_captures_from_left ^= BITBOARD_POSITION(pos_to);
        }

        while(pawn_promotion_captures_from_right) {
            int pos_to = BITBOARD_find_bit(pawn_promotion_captures_from_right);
            num_moves += STATE_add_move_to_list_promotion_capture(pos_to, pos_to + attack_from_right, capture_type[pos_to], moves + num_moves);
            pawn_promotion_captures_from_right ^= BITBOARD_POSITION(pos_to);
        }

        /* Promotion */
        while(pawn_promotion) {
            int pos_to = BITBOARD_find_bit(pawn_promotion);
            int pos_from = pos_to + step;
            num_moves += STATE_add_move_to_list_promotion(pos_to, pos_from, moves + num_moves);
            pawn_promotion ^= BITBOARD_POSITION(pos_to);
        }

        /* Pawn push */
        while(possible_moves) {
            int pos_to = BITBOARD_find_bit(possible_moves);
            STATE_add_move_to_list(pos_to, pos_to + step, PAWN, 0, MOVE_QUIET, moves + num_moves++);
            possible_moves ^= BITBOARD_POSITION(pos_to);
        }
        /* Double push */
        while(pawn_push2) {
            int pos_to = BITBOARD_find_bit(pawn_push2);
            STATE_add_move_to_list(pos_to, pos_to + 2 * step, PAWN, 0, MOVE_DOUBLE_PAWN_PUSH, moves + num_moves++);
            pawn_push2 ^= BITBOARD_POSITION(pos_to);
        }
    }

    /* Loop through all types of pieces */
    for(int type = KNIGHT; type < NUM_TYPES - 1; type++) {
        bitboard_t pieces = s->bitboard[player_index + type];

        while(pieces) { /* Loop through all pieces of the type */
            /* Get one position from the bitboard */
            int pos_from = BITBOARD_find_bit(pieces);

            /* Get all possible moves for this piece */
            bitboard_t possible_moves, possible_captures;
            MOVEGEN_piece(type, pos_from, player_pieces, opponent_pieces, &possible_moves, &possible_captures);
            
            while(possible_captures) {
                int pos_to = BITBOARD_find_bit(possible_captures);
                STATE_add_move_to_list(pos_to, pos_from, type, capture_type[pos_to], MOVE_CAPTURE, moves + num_moves);
                num_moves++;
                possible_captures ^= BITBOARD_POSITION(pos_to);
            }

            while(possible_moves) {
                int pos_to = BITBOARD_find_bit(possible_moves);
                STATE_add_move_to_list(pos_to, pos_from, type, 0, MOVE_QUIET, moves + num_moves);
                num_moves++;
                possible_moves ^= BITBOARD_POSITION(pos_to);
            }
        
            /* Clear position from bitboard */
            pieces ^= BITBOARD_POSITION(pos_from);
        }
    }
    
    /* En passant */
    if(s->ep_file != STATE_EN_PASSANT_NONE) {
        int file;
        bitboard_t attack_file;
        
        /* The file of the possible en passant capture */
        file = s->ep_file;
        attack_file = BITBOARD_FILE << file;
        
        /* Find pawns that can make the capture */
        bitboard_t pieces = bitboard_ep_capturers[player][file] & s->bitboard[player_index+PAWN];
        
        /* Loop through the found pawns */
        while(pieces) {
            int pos_from = BITBOARD_find_bit(pieces);
            int pos_to = BITBOARD_find_bit(bitboard_pawn_capture[player][pos_from] & attack_file);
            STATE_add_move_to_list(pos_to, pos_from, PAWN, PAWN, MOVE_EP_CAPTURE, moves + num_moves);
            num_moves++;
            pieces ^= BITBOARD_POSITION(pos_from);
        }
    }

    /* King-side Castling */
    if(s->castling[player] & STATE_FLAGS_KING_CASTLE_POSSIBLE_MASK) {
        if((bitboard_king_castle_empty[player] & s->bitboard[OCCUPIED]) == 0) {
            int king_pos = BITBOARD_find_bit(s->bitboard[player_index + KING]);
            if(EVAL_position_is_attacked(s, player, king_pos+0) == 0 && 
               EVAL_position_is_attacked(s, player, king_pos+1) == 0 &&
               EVAL_position_is_attacked(s, player, king_pos+2) == 0)
            {
                STATE_add_move_to_list(king_pos+2, king_pos, KING, 0, MOVE_KING_CASTLE, moves + num_moves);
                num_moves++;
            }
        }
    }
    
    /* Queen-side Castling */
    if(s->castling[player] & STATE_FLAGS_QUEEN_CASTLE_POSSIBLE_MASK) {
        if((bitboard_queen_castle_empty[player] & s->bitboard[OCCUPIED]) == 0) {
            int king_pos = BITBOARD_find_bit(s->bitboard[player_index + KING]);
            if(EVAL_position_is_attacked(s, player, king_pos-0) == 0 && 
               EVAL_position_is_attacked(s, player, king_pos-1) == 0 &&
               EVAL_position_is_attacked(s, player, king_pos-2) == 0)
            {
                STATE_add_move_to_list(king_pos-2, king_pos, KING, 0, MOVE_QUEEN_CASTLE, moves + num_moves);
                num_moves++;
            }
        }
    }

    return num_moves;
}

int STATE_generate_moves_quiescence(const chess_state_t *s, move_t *moves)
{
    int num_moves = 0;
    const int player = s->player;
    const int opponent = player ^ 1;
    const int player_index = NUM_TYPES*player;
    const int opponent_index = NUM_TYPES*opponent;
    const bitboard_t player_pieces = s->bitboard[player_index + ALL];
    const bitboard_t opponent_pieces = s->bitboard[opponent_index + ALL];
    int capture_type[64];

    for(int opponent_type = PAWN; opponent_type < KING; opponent_type++) {
        bitboard_t pieces = s->bitboard[opponent_index + opponent_type];
        while(pieces) {
            int pos = BITBOARD_find_bit(pieces);
            capture_type[pos] = opponent_type;
            pieces ^= BITBOARD_POSITION(pos);
        }
    }

    /* Pawns */
    {
        bitboard_t pieces = s->bitboard[player_index + PAWN];
        bitboard_t possible_moves, possible_captures;
        bitboard_t pawn_push2, pawn_captures_from_left, pawn_captures_from_right;
        bitboard_t pawn_promotion, pawn_promotion_captures_from_left, pawn_promotion_captures_from_right;
        MOVEGEN_all_pawns(player, pieces, player_pieces, opponent_pieces, &possible_moves, &pawn_push2, &pawn_captures_from_left, &pawn_captures_from_right, &pawn_promotion, &pawn_promotion_captures_from_left, &pawn_promotion_captures_from_right);

        int attack_from_left, attack_from_right, step;

        if(player == WHITE) {
            attack_from_left = -9;
            attack_from_right = -7;
            step = -8;
        } else {
            attack_from_left = 7;
            attack_from_right = 9;
            step = 8;
        }

        /* Captures */
        while(pawn_captures_from_left) {
            int pos_to = BITBOARD_find_bit(pawn_captures_from_left);
            STATE_add_move_to_list(pos_to, pos_to + attack_from_left, PAWN, capture_type[pos_to], MOVE_CAPTURE, moves + num_moves++);
            pawn_captures_from_left ^= BITBOARD_POSITION(pos_to);
        }

        while(pawn_captures_from_right) {
            int pos_to = BITBOARD_find_bit(pawn_captures_from_right);
            STATE_add_move_to_list(pos_to, pos_to + attack_from_right, PAWN, capture_type[pos_to], MOVE_CAPTURE, moves + num_moves++);
            pawn_captures_from_right ^= BITBOARD_POSITION(pos_to);
        }

        /* Promotion with capture */
        while(pawn_promotion_captures_from_left) {
            int pos_to = BITBOARD_find_bit(pawn_promotion_captures_from_left);
            num_moves += STATE_add_move_to_list_promotion_capture(pos_to, pos_to + attack_from_left, capture_type[pos_to], moves + num_moves);
            pawn_promotion_captures_from_left ^= BITBOARD_POSITION(pos_to);
        }

        while(pawn_promotion_captures_from_right) {
            int pos_to = BITBOARD_find_bit(pawn_promotion_captures_from_right);
            num_moves += STATE_add_move_to_list_promotion_capture(pos_to, pos_to + attack_from_right, capture_type[pos_to], moves + num_moves);
            pawn_promotion_captures_from_right ^= BITBOARD_POSITION(pos_to);
        }

        /* Promotion */
        while(pawn_promotion) {
            int pos_to = BITBOARD_find_bit(pawn_promotion);
            int pos_from = pos_to + step;
            num_moves += STATE_add_move_to_list_promotion(pos_to, pos_from, moves + num_moves);
            pawn_promotion ^= BITBOARD_POSITION(pos_to);
        }
    }

    /* Loop through all types of pieces */
    for(int type = KNIGHT; type < NUM_TYPES - 1; type++) {
        bitboard_t pieces = s->bitboard[player_index + type];

        while(pieces) { /* Loop through all pieces of the type */
            /* Get one position from the bitboard */
            int pos_from = BITBOARD_find_bit(pieces);

            /* Get all possible moves for this piece */
            bitboard_t possible_moves, possible_captures;
            MOVEGEN_piece(type, pos_from, player_pieces, opponent_pieces, &possible_moves, &possible_captures);
            
            while(possible_captures) {
                int pos_to = BITBOARD_find_bit(possible_captures);
                STATE_add_move_to_list(pos_to, pos_from, type, capture_type[pos_to], MOVE_CAPTURE, moves + num_moves);
                num_moves++;
                possible_captures ^= BITBOARD_POSITION(pos_to);
            }
        
            /* Clear position from bitboard */
            pieces ^= BITBOARD_POSITION(pos_from);
        }
    }
    
    /* En passant */
    if(s->ep_file != STATE_EN_PASSANT_NONE) {
        int file;
        bitboard_t attack_file;
        
        /* The file of the possible en passant capture */
        file = s->ep_file;
        attack_file = BITBOARD_FILE << file;
        
        /* Find pawns that can make the capture */
        bitboard_t pieces = bitboard_ep_capturers[player][file] & s->bitboard[player_index+PAWN];
        
        /* Loop through the found pawns */
        while(pieces) {
            int pos_from = BITBOARD_find_bit(pieces);
            int pos_to = BITBOARD_find_bit(bitboard_pawn_capture[player][pos_from] & attack_file);
            STATE_add_move_to_list(pos_to, pos_from, PAWN, PAWN, MOVE_EP_CAPTURE, moves + num_moves);
            num_moves++;
            pieces ^= BITBOARD_POSITION(pos_from);
        }
    }

    return num_moves;
}

int STATE_apply_move(chess_state_t *s, const move_t move)
{
    int player = s->player;
    int opponent = player ^ 1;

    /* Reset en passant state */
    s->hash ^= bitboard_zobrist_ep[(int)s->ep_file];
    s->ep_file = STATE_EN_PASSANT_NONE;

    if(move) {
        int pos_from        = MOVE_GET_POS_FROM(move);
        int pos_to          = MOVE_GET_POS_TO(move);
        int type            = MOVE_GET_TYPE(move);
        int special         = MOVE_GET_SPECIAL_FLAGS(move);

        int player_index = player * NUM_TYPES;
        int opponent_index = NUM_TYPES - player_index;

        /* Reset castling hash */
        s->hash ^= bitboard_zobrist_castling[player][(int)s->castling[player]];
        
        /* Increment half-move clock */
        s->halfmove_clock++;
        
        /* Remove bitboard of this type from the "ALL bitboard" */
        s->bitboard[player_index + ALL] ^= s->bitboard[player_index + type];

        /* Clear "from", set "to" */
        BITBOARD_CLEAR(s->bitboard[player_index + type], pos_from);
        BITBOARD_SET(s->bitboard[player_index + type], pos_to);

        /* Update the "ALL bitboard" */
        s->bitboard[player_index + ALL] ^= s->bitboard[player_index + type];
        
        /* Update hash with normal move */
        s->hash ^= bitboard_zobrist[player][type][pos_from];
        s->hash ^= bitboard_zobrist[player][type][pos_to];
        
        /* Remove captured piece from the other side */
        if(special & MOVE_CAPTURE) {
            int opponent_type = MOVE_GET_CAPTURE_TYPE(move);
            if(special == MOVE_EP_CAPTURE) {
                int pos_capture = BITBOARD_find_bit(bitboard_ep_capture[pos_to]);
                
                /* En passant capture */
                s->bitboard[opponent_index + PAWN] ^= bitboard_ep_capture[pos_to];
                s->bitboard[opponent_index + ALL] ^= bitboard_ep_capture[pos_to];
                
                /* Update hashes with EP capture */
                s->hash ^= bitboard_zobrist[opponent][opponent_type][pos_capture];

            } else {
                /* Normal capture */
                BITBOARD_CLEAR(s->bitboard[opponent_index + opponent_type], pos_to);
                BITBOARD_CLEAR(s->bitboard[opponent_index + ALL], pos_to);
            
                /* Rook capture disables castling */
                if((opponent_type == ROOK) && (BITBOARD_POSITION(pos_to) & bitboard_start_position[opponent][ROOK])) {
                    s->hash ^= bitboard_zobrist_castling[opponent][(int)s->castling[opponent]];
                    if(bitboard_file[pos_to] == bitboard_file[0]) {
                        s->castling[opponent] &= ~STATE_FLAGS_QUEEN_CASTLE_POSSIBLE_MASK;
                    } else {
                        s->castling[opponent] &= ~STATE_FLAGS_KING_CASTLE_POSSIBLE_MASK;
                    }
                    s->hash ^= bitboard_zobrist_castling[opponent][(int)s->castling[opponent]];
                }
                
                /* Update hash with normal capture */
                s->hash ^= bitboard_zobrist[opponent][opponent_type][pos_to];
            }
            
            /* Reset half-move clock when a piece is captured */
            s->halfmove_clock = 0;
        }
        
        if(type == KING) {
            /* Disable castling */
            s->castling[player] &= ~(STATE_FLAGS_KING_CASTLE_POSSIBLE_MASK | STATE_FLAGS_QUEEN_CASTLE_POSSIBLE_MASK);
            
            /* Castling */
            if(special == MOVE_KING_CASTLE) {
                /* Move rook */
                s->bitboard[player_index + ALL] ^= s->bitboard[player_index + ROOK];
                s->bitboard[player_index + ROOK] ^= s->bitboard[player_index + KING] << 1;
                s->bitboard[player_index + ROOK] ^= s->bitboard[player_index + KING] >> 1;
                s->bitboard[player_index + ALL] ^= s->bitboard[player_index + ROOK];
                
                /* Update hash with king-side castling */
                s->hash ^= bitboard_zobrist[player][ROOK][pos_to+1];
                s->hash ^= bitboard_zobrist[player][ROOK][pos_to-1];
            }
            if(special == MOVE_QUEEN_CASTLE) {
                /* Move rook */
                s->bitboard[player_index + ALL] ^= s->bitboard[player_index + ROOK];
                s->bitboard[player_index + ROOK] ^= s->bitboard[player_index + KING] >> 2;
                s->bitboard[player_index + ROOK] ^= s->bitboard[player_index + KING] << 1;
                s->bitboard[player_index + ALL] ^= s->bitboard[player_index + ROOK];
                
                /* Update hash with king-side castling */
                s->hash ^= bitboard_zobrist[player][ROOK][pos_to-2];
                s->hash ^= bitboard_zobrist[player][ROOK][pos_to+1];
            }
        }
        
        if(type == ROOK) {
            /* Disable castling for a moved rook */
            if(BITBOARD_POSITION(pos_from) & bitboard_start_position[player][ROOK]) {
                if(bitboard_file[pos_from] == bitboard_file[0]) {
                    s->castling[player] &= ~STATE_FLAGS_QUEEN_CASTLE_POSSIBLE_MASK;
                } else {
                    s->castling[player] &= ~STATE_FLAGS_KING_CASTLE_POSSIBLE_MASK;
                }
            }
        }
        
        if(type == PAWN) {
            /* Reset half-move clock when a pawn is moved */
            s->halfmove_clock = 0;
                  
            /* Pushing pawn 2 squares opens for en passant */
            if(special == MOVE_DOUBLE_PAWN_PUSH) {
                int file = BITBOARD_GET_FILE(pos_from);
                if(bitboard_ep_capturers[opponent][file] & s->bitboard[opponent_index+PAWN]) {
                    s->ep_file = (unsigned char)file;
                    s->hash ^= bitboard_zobrist_ep[file];
                }
            }
            
            /* Pawn promotion */
            if(MOVE_IS_PROMOTION(move)) {
                int promotion_type = MOVE_PROMOTION_TYPE(move);
                BITBOARD_CLEAR(s->bitboard[player_index + type], pos_to);
                BITBOARD_SET(s->bitboard[player_index + promotion_type], pos_to);
                
                /* Update hash with promotion */
                s->hash ^= bitboard_zobrist[player][PAWN][pos_to];
                s->hash ^= bitboard_zobrist[player][promotion_type][pos_to];
            }
        }

        /* Occupied by piece of any color */
        s->bitboard[OCCUPIED] = s->bitboard[WHITE_PIECES+ALL] | s->bitboard[BLACK_PIECES+ALL];

        /* Apply castling rights to hash */
        s->hash ^= bitboard_zobrist_castling[player][(int)s->castling[player]];
    }    
    /* Switch side to play */
    s->player = (unsigned char)opponent;
    s->hash ^= bitboard_zobrist_color;
    
    /* Store last move */
    s->last_move = move;

    return 0;
}

void STATE_compute_hash(chess_state_t *s)
{
    int color;
    int type;
    int pos;
    bitboard_t pieces;
    
    s->hash = 0;
    
    for(color = 0; color < NUM_COLORS; color++) {
        for(type = 0; type < NUM_TYPES - 1; type++) {
            pieces = s->bitboard[color*NUM_TYPES + type];

            while(pieces) {
                pos = BITBOARD_find_bit(pieces);

                /* Update the hash for each piece on the board */
                s->hash ^= bitboard_zobrist[color][type][pos];
                
                pieces ^= BITBOARD_POSITION(pos);
            }
        }
    }

    /* En passant */
    s->hash ^= bitboard_zobrist_ep[(int)s->ep_file];

    /* Castling rights */
    s->hash ^= bitboard_zobrist_castling[WHITE][(int)s->castling[WHITE]];
    s->hash ^= bitboard_zobrist_castling[BLACK][(int)s->castling[BLACK]];

    if(s->player == WHITE) {
        s->hash ^= bitboard_zobrist_color;
    }
}

int STATE_risk_zugzwang(const chess_state_t *s)
{
    /* Risk of zugzwang if only king and pawns remain on the playing side */
    const int player_index = NUM_TYPES*(int)s->player;
    return s->bitboard[player_index+ALL] == (s->bitboard[player_index+PAWN] | s->bitboard[player_index+KING]);
}

void STATE_move_print_debug(const move_t move)
{
    int pos_from = MOVE_GET_POS_FROM(move);
    int pos_to   = MOVE_GET_POS_TO(move);
    fprintf(stdout, "#%c%c -> %c%c\n", BITBOARD_GET_FILE(pos_from)+'A', BITBOARD_GET_RANK(pos_from)+'1', BITBOARD_GET_FILE(pos_to)+'A', BITBOARD_GET_RANK(pos_to)+'1');
}

void STATE_board_print_debug(const chess_state_t *s)
{
    int rank, file, color;
    for(rank = 7; rank >= 0; rank--) {
        fprintf(stdout, "#%c ", rank + '1');
        for(file = 0; file < 8; file++) {
            char c = '-';
            for(color = WHITE; color <= BLACK; color++) {
                if(s->bitboard[color*NUM_TYPES+ALL] & BITBOARD_RANK_FILE(rank, file)) {
                    if(s->bitboard[color*NUM_TYPES+PAWN] & BITBOARD_RANK_FILE(rank, file)) c = 'P';
                    if(s->bitboard[color*NUM_TYPES+KNIGHT] & BITBOARD_RANK_FILE(rank, file)) c = 'N';
                    if(s->bitboard[color*NUM_TYPES+BISHOP] & BITBOARD_RANK_FILE(rank, file)) c = 'B';
                    if(s->bitboard[color*NUM_TYPES+ROOK] & BITBOARD_RANK_FILE(rank, file)) c = 'R';
                    if(s->bitboard[color*NUM_TYPES+QUEEN] & BITBOARD_RANK_FILE(rank, file)) c = 'Q';
                    if(s->bitboard[color*NUM_TYPES+KING] & BITBOARD_RANK_FILE(rank, file)) c = 'K';
                    if(color == BLACK) c += 32;
                }
            }
            fprintf(stdout, "%c ", c);
        }
        fprintf(stdout, "\n");
    }
    fprintf(stdout, "   A B C D E F G H\n");
}
