#include <stdio.h>
#include <stdlib.h>
#include <grass/gis.h>
#include <grass/vector.h>
#include <grass/dbmi.h>
#include <grass/glocale.h>


double compute_line_nodes_angle(struct line_pnts *points)
{
    double x_start, y_start, z;
    double x_end, y_end;
    double x, y;
    int n_points = Vect_get_num_line_points(points);

    if (n_points < 2)
        return (-9.0);

    Vect_line_get_point(points, 0, &x_start, &y_start, &z);
    Vect_line_get_point(points, n_points - 1, &x_end, &y_end, &z);

    x = x_end - x_start;
    y = y_end - y_start;

    if (y == 0.0 && x == 0.0)
        return (0.0);
    else
        return (atan2(y, x));
}

/*\brief Compute angle of two lines, which is defined by start and end point of the lines.

   Parameters from_dir, to_dir defines defines line direction to node for which is angle defined 
   (negative - line goes from node / positive line goes into node).

   Angle is zero when lines are straight. If line_pnts_j to is on the left from line_pnts_i 
   the angle is negative.

   \return lines angle
   \return -9.0 if line is defined by one point or has same start and end point
 */

double compute_lines_angle(struct line_pnts *line_pnts_from, int from_dir, 
                            struct line_pnts *line_pnts_to, int to_dir)
{
    double angle_from, angle_to;
    double angle;

    double x1, x2, y1, y2, z;

    int n_points_from = Vect_get_num_line_points(line_pnts_from);
    int n_points_to = Vect_get_num_line_points(line_pnts_to);

    Vect_line_get_point(line_pnts_from, 0, &x1, &y1, &z);
    Vect_line_get_point(line_pnts_from, n_points_from - 1, &x2, &y2, &z);

    if (x1 == x2 && y1 == y2)
         return -9.0;

    Vect_line_get_point(line_pnts_to, 0, &x1, &y1, &z);
    Vect_line_get_point(line_pnts_to, n_points_to - 1, &x2, &y2, &z);
    if (x1== x2 && y1 == y2)
         return -9.0;

    if (from_dir > 0)
        Vect_line_reverse(line_pnts_from);

    if (to_dir < 0)
        Vect_line_reverse(line_pnts_to);

    angle_from = compute_line_nodes_angle(line_pnts_from);
    angle_to = compute_line_nodes_angle(line_pnts_to);

    if (angle_from == -9.0)
        angle = angle_from;
    else if (angle_to == -9.0)
        angle = angle_to;
    else {
        angle = angle_from - angle_to;

        if (angle > M_PI)
            angle = -2 * M_PI + angle;

        if (angle < -M_PI)
            angle = 2 * M_PI + angle;
    }

    /* reverse it back to original order */
    if (from_dir > 0)
        Vect_line_reverse(line_pnts_from);

    if (to_dir < 0)
        Vect_line_reverse(line_pnts_to);

    return angle;
}

/*\brief Add line uturns into turntable.
        
    Add two records into turntable because every line has two possible U-turns.
*/
int add_uturn(dbDriver * driver, char *ttb_name, int * next_ttb_cat, int ln_cat, int isec_start_cat, int isec_end_cat){
    int i, isec;
    dbString db_buf;
    char buf[DB_SQL_MAX];
    db_init_string(&db_buf);

    ln_cat = abs(ln_cat);

    isec = isec_end_cat;
    for(i = 0; i < 2; ++i) {
        if(i == 1) {
            ln_cat = -1 * ln_cat;
            isec = isec_start_cat;
        }
        /* cat, ln_from, ln_to, cost, isec, angle */
        sprintf(buf,
                "INSERT INTO %s values ( %d, %d, %d, %f, %d, %f);",
                ttb_name, (*next_ttb_cat), ln_cat, ln_cat * -1, 0.0,
                isec, M_PI);
        db_set_string(&db_buf, buf);

        G_debug(3, "Adding u-turn into turntable:\n%s", db_get_string(&db_buf));

        if (db_execute_immediate(driver, &db_buf) != DB_OK){
            db_free_string(&db_buf);
            return -1;
        }
        ++(*next_ttb_cat);
    }

    db_free_string(&db_buf);
    return 1;
}


/*\brief Add turns for two lines into turntable.
        
    Add two records into turntable because we can take the turn from two opposite directions.
*/
int add_turns(dbDriver * driver, char *ttb_name, int * next_ttb_cat, 
             int ln_i_cat, struct line_pnts *line_pnts_i, int ln_j_cat, struct line_pnts *line_pnts_j, int isec_cat){
    int i, combinations;
    int ln_f, ln_t;
    dbString db_buf;
    char buf[DB_SQL_MAX];
    double angle;

    db_init_string(&db_buf);

    int ln_j_dir, ln_i_dir;
    int ln_to_cat, ln_from_cat;

    ln_i_dir = ln_i_cat;
    ln_j_dir = ln_j_cat;

    /* compute angle if the lines angle is computed from ln_from_cat to ln_to_cat */
    if (ln_to_cat == ln_i_cat)
        angle =
        compute_lines_angle(line_pnts_j, ln_j_dir, 
                            line_pnts_i,  ln_i_dir);
    else
        angle =
            compute_lines_angle(line_pnts_i, ln_i_dir,
                                line_pnts_j, ln_j_dir);

    ln_i_cat = abs(ln_i_cat);
    ln_j_cat = abs(ln_j_cat);

    ln_from_cat = ln_i_cat;
    ln_to_cat = ln_j_cat;

    /*Find right lines nodes (positive or negative), will be connected by the turn.*/
    if (ln_j_dir < 0 && ln_i_dir < 0)
        ln_to_cat *= -1;

    else if (ln_j_dir > 0 && ln_i_dir > 0)
        ln_from_cat *= -1;

    else if (ln_j_dir < 0) {
        ln_to_cat = ln_i_cat;
        ln_from_cat = ln_j_cat;
    }

    ln_f = ln_from_cat;
    ln_t = ln_to_cat;

    /*Create first turn from i to j, then create turn in the opposite direction.*/
    for(i = 0; i < 2; ++i) {

        /* connect right nodes for opposite turn */
        if(i == 1) {
            ln_f = ln_to_cat * -1;
            ln_t = ln_from_cat * -1;
        }

        /* cat, ln_from, ln_to, cost, isec, angle */
        sprintf(buf,
                "INSERT INTO %s values ( %d, %d, %d, %f, %d,",
                ttb_name, (*next_ttb_cat), ln_f, ln_t, 0.0,
                isec_cat);
        db_set_string(&db_buf, buf);

        if (angle == -9.0)
            db_append_string(&db_buf, "NULL)");
        else {
            /* the angle is on the other side in opposite turn (e. g. left -> right)*/
            if(i == 1)
                angle *= -1;

            sprintf(buf, "%f)", angle);
            db_append_string(&db_buf, buf);
        }

        G_debug(3, "Adding turn into turntable:\n%s", db_get_string(&db_buf));

        if (db_execute_immediate(driver, &db_buf) != DB_OK){
            db_free_string(&db_buf);
            return -1;
        }
        ++(*next_ttb_cat);
    }

    db_free_string(&db_buf);
    return 1;

}


void populate_turntable(dbDriver * driver, struct Map_info *InMap,
                        struct Map_info *OutMap, char *ttb_name,
                        int tfield, int tucfield, int a_field, int arc_type)
{
    char buf[DB_SQL_MAX];
    struct ilist list;

    int *features_id;

    int n_node_lines, n_features, i_line, j_line, next_ttb_cat, i_ucat,
        i_rew_lines, n_lines;
    int n_nodes, pivot_node, outside_node, isec_start_ucat, isec_end_ucat, node1, node2;
    int ln_i_id, ln_j_id, ln_i_ucat, ln_j_ucat;

    int ltype_i, ltype_j;

    struct line_pnts *line_pnts_i, *line_pnts_j;
    struct line_cats *cats_i, *cats_j;

    double x, y, z;

    line_pnts_i = Vect_new_line_struct();
    line_pnts_j = Vect_new_line_struct();
    cats_i = Vect_new_cats_struct();
    cats_j = Vect_new_cats_struct();

    n_lines = 0;
    if(arc_type & GV_LINE)
        n_lines += Vect_get_num_primitives(InMap, GV_LINE);
    if(arc_type & GV_BOUNDARY)
        n_lines += Vect_get_num_primitives(InMap, GV_BOUNDARY);

    /*Converts feature input map id into current id in output map. 
       When the feature is rewritten, it's original state still exists 
       just marked as dead. The new feature is written to first possible 
       position and that is the id (all dead + all alive features + 1).

       If feature id is 0 the feature was not already written into
       output map. */

    n_features = Vect_get_num_lines(InMap);

    G_debug(3, "Found %d line features in <%s> vector map", n_features,
            InMap->name);

    features_id = G_malloc(sizeof(int) * n_features);
    G_zero(features_id, sizeof(int) * n_features);

    n_nodes = Vect_get_num_nodes(InMap);
    G_debug(3, "Found %d nodes in <%s> vector map", n_nodes, InMap->name);

    db_begin_transaction(driver);

    /* Stores category for a next record/turn in turntable. */
    next_ttb_cat = 1;

    /* Stores number of category which will be assigned to a next feature added into tucfield. */
    i_ucat = 1;

    /* Stores number of rewritten features. If feature is rewritten, it can be sought by sum of
       i_ucat + i_rew_lines variables values (id), when was rewritten. 
       The value is saved into features_id array. */
    i_rew_lines = 0;

    G_init_ilist(&list);

    /* Every node represents one intersection. */
    for (pivot_node = 1; pivot_node <= n_nodes; pivot_node++) {
        n_node_lines = Vect_get_node_n_lines(InMap, pivot_node);

        G_debug(3, "Found %d lines connected to node with id %d",
                n_node_lines, pivot_node);

        /*Creates record in turntable for every possible turn 
           in intersection defined by node and lines which meets on the node.

           It also takes in account  U-turns. */

        for (i_line = 0; i_line < n_node_lines; i_line++) {

            ln_i_id = Vect_get_node_line(InMap, pivot_node, i_line);

            /*Line was not written into output map. */
            if (features_id[abs(ln_i_id) - 1] < 1) {
                ltype_i = Vect_read_line(InMap, line_pnts_i, cats_i, abs(ln_i_id));
                
                /* If line does not belong into arc layer, skip it. */
                if (Vect_field_cat_get(cats_i, a_field, &list) < 0 || !(ltype_i & arc_type))
                    continue;

                /* Delete categories in tfield and tucfield if they are in input map defined. */
                Vect_field_cat_del(cats_i, tfield, -1);
                Vect_field_cat_del(cats_i, tucfield, -1);
            }

            /*If i line was already written into output map, 
               we need to take it's categories from the output map with categories 
               for tlayer and tuclayer. */
            else {
                ln_i_id = Vect_get_node_line(InMap, pivot_node, i_line);

                ltype_i = Vect_read_line(OutMap, line_pnts_i, cats_i,
                                         features_id[abs(ln_i_id) - 1]);
                Vect_cat_get(cats_i, tucfield, &ln_i_ucat);
                
                /* add line direction information to ucat */
                if(ln_i_id < 0)
                    ln_i_ucat *= -1;

            }

            for (j_line = i_line; j_line < n_node_lines; j_line++) {

                ln_j_id = Vect_get_node_line(InMap, pivot_node, j_line);

                /* write line, which have not been written into new map yet. */
                if (features_id[abs(ln_j_id) - 1] < 1) {
                    /* Get line from input map. */
                    ltype_j = Vect_read_line(InMap, line_pnts_j, cats_j, abs(ln_j_id));

                    /* If line does not belong into arc layer, skip it. */
                    if (Vect_field_cat_get(cats_j, a_field, &list) < 0 || !(ltype_i & arc_type))
                        continue;

                    /* Delete categories in tfield and tucfield if they are in input map defined. */
                    Vect_field_cat_del(cats_j, tfield, -1);
                    Vect_field_cat_del(cats_j, tucfield, -1);

                    /* Write U-turn for every not yet written line. */

                    /* Assign unique category (assigned only when feature is written). */
                    Vect_cat_set(cats_j, tucfield, i_ucat);

                    /* Assign turn category in turntable for the U-turn. */
                    Vect_cat_set(cats_j, tfield, next_ttb_cat);
                    Vect_cat_set(cats_j, tfield, next_ttb_cat + 1);

                    /* If i line and j line are different, we also need to insert a turn which is defined
                       by these two edges, therefore we need to add new tfield category to j line, which is corresponding
                       to the turn. */
                    if (ln_j_id != ln_i_id) {
                        Vect_cat_set(cats_j, tfield, next_ttb_cat);
                        Vect_cat_set(cats_j, tfield, next_ttb_cat + 1);
                    }

                    ln_j_ucat = i_ucat;
                    
                    /* add line direction information to ucat */
                    if(ln_j_id < 0)
                        ln_j_ucat *= -1;

                    /* We create two nodes in turntable for every line. These nodes have 
                       positive and negative values, with their absolute values identical.

                       Every node corresponds to opposite line direction. The positive node 
                       matches the direction of line. The negative node matches the opposite direction.

                       Imagine that you are standing on some road/line before a intersection wanting to cross it.
                       If you are going to cross intersection, which is in line direction,
                       you are standing on the POSITIVE NODE. If you would cross the intersection from any other line 
                       to the line, you would come into the NEGATIVE NODE. 

                       These two nodes are connected with U-turns, which are two for both direction. 
                       Every U-turn direction belongs to the another intersection. U-turn from the POSITIVE NODE 
                       to the NEGATIVE one belongs to the intersection we are going to cross. The other U-turn belongs 
                       to a intersection in opposite end of the line.

                       Turntable columns:

                       cat - category in tfield (layer with turntable), which are hold by both ln_from and ln_to lines 
                       ln_from - unique category in tucfield assigned to the line 
                       ln_to - unique category in tucfield assigned to the line 
                       cost - cost for turn from ln_from to ln_to
                       isec - point category in tucfield, which represents the intersection, where the turn belongs
                       angle - in radians, see comments in compute_lines_angle function, it is PI for U-turns
                     */

                    /* Find second node (outside_node) of the line. */
                    Vect_get_line_nodes(InMap, abs(ln_j_id), &node1, &node2);

                    if (node1 == pivot_node)
                        outside_node = node2;
                    else
                        outside_node = node1;

                    /* Decide intersection where U-turns belongs. */
                    if (ln_j_id < 0) {
                        isec_start_ucat = outside_node + n_lines;
                        isec_end_ucat = pivot_node + n_lines;
                    }
                    else {
                        isec_start_ucat = pivot_node + n_lines;
                        isec_end_ucat = outside_node + n_lines;
                    }

                   
                    /* If i and j lines are identical, write these categories also into i line,
                       otherwise they would be forgotten during rewriting of i line. */
                    if (ln_j_id == ln_i_id) {
                        Vect_cat_set(cats_i, tfield, next_ttb_cat);
                        Vect_cat_set(cats_i, tfield, next_ttb_cat + 1);
                        Vect_cat_set(cats_i, tucfield, i_ucat);
                    }

                    if (add_uturn(driver, ttb_name, &next_ttb_cat, abs(ln_j_ucat), isec_start_ucat, isec_end_ucat) < 0) {
                        G_free(features_id);

                        Vect_destroy_line_struct(line_pnts_i);
                        Vect_destroy_line_struct(line_pnts_j);
                        Vect_destroy_cats_struct(cats_i);
                        Vect_destroy_cats_struct(cats_j);

                        G_fatal_error(_("Unable to insert data into turntable."));
                    }

                    /* increment unique category number for next line, which will be written */
                    ++i_ucat;

                    /* If i line and j line are different, we also need to insert a turn which is defined
                       by these two edges, therefore we need to add new category to j line, which is corresponding
                       to the turn.  
                       */
                    if (abs(ln_j_id) != abs(ln_i_id))
                    {
                        Vect_cat_set(cats_j, tfield, next_ttb_cat);
                        Vect_cat_set(cats_j, tfield, next_ttb_cat + 1);
                    }
                    
                    /* Write new line into output map and save it's id to be possible to find it and edit it later 
                       (when we get to intersection, which is in other end of the line.) */
                    features_id[abs(ln_j_id) - 1] =
                        Vect_write_line(OutMap, ltype_j, line_pnts_j, cats_j);

                    /* i, j lines  are equal, it consists only U-turn
                       Absolute values are there because in case of the lines which have same start and end point, we do not want 
                       to create redundant lines between these points. This combination has been already done by uturn method.
                     */
                    if (abs(ln_j_id) == abs(ln_i_id)) {
                        /* remember unique category also for i line */
                        ln_i_ucat = ln_j_ucat;
                        continue;
                    }
                }
                /* skip if i, j lines are same (U-turn was already written) */
                else if (abs(ln_j_id) == abs(ln_i_id))
                    continue;
                /* write new turn combination for already written i, j lines into output map */
                else {
                    /* Get modified cats from out map also for j line, which was already written and 
                       cats differ from the former cats in the line in input map. */
                    ltype_j = Vect_read_line(OutMap, line_pnts_j, cats_j,
                                             features_id[abs(ln_j_id) - 1]);

                    /* set category in turntable for new turn, which will be written */
                    Vect_cat_set(cats_j, tfield, next_ttb_cat);
                    Vect_cat_set(cats_j, tfield, next_ttb_cat + 1);

                    /* get already assigned unique category of the j line 
                       (used for ln_from_cat or ln_to_cat in turntable) */
                    Vect_cat_get(cats_j, tucfield, &ln_j_ucat);

                    /* add line direction information to ucat */
                    if(ln_j_id < 0)
                        ln_j_ucat *= -1;

                    /* rewrite j line with added the new category for the turn */
                    Vect_rewrite_line(OutMap, features_id[abs(ln_j_id) - 1],
                                      ltype_j, line_pnts_j, cats_j);

                    /* Because of rewriting, the id of j line was changed, 
                       therefore we have to update it in features_id. */
                    features_id[abs(ln_j_id) - 1] = i_ucat + i_rew_lines;

                    /* increment number of rewritten elements,  for more see initialization of the variable */
                    ++i_rew_lines;
                }

                /* We have to decide which nodes will be connected, which depends on lines directions.
                   Line direction information is stored in ln_i_id/ln_j_id variables. It the variable is 
                   negative, the line goes into the intersection. If it is positive the line goes from the 
                   intersection. */


                /* The turn belongs to same intersection regardless the direction. Only exception are the U-turns. */
                isec_start_ucat = isec_end_ucat = pivot_node + n_lines;

                Vect_cat_set(cats_i, tfield, next_ttb_cat);
                Vect_cat_set(cats_i, tfield, next_ttb_cat + 1);

                if (add_turns(driver, ttb_name, &next_ttb_cat, 
                             ln_i_ucat,line_pnts_i, ln_j_ucat, line_pnts_j, isec_start_ucat) < 0) {
                    G_free(features_id);

                    Vect_destroy_line_struct(line_pnts_i);
                    Vect_destroy_line_struct(line_pnts_j);
                    Vect_destroy_cats_struct(cats_i);
                    Vect_destroy_cats_struct(cats_j);

                    G_fatal_error(_("Unable to insert data into turntable."));
                }

            }

            /* rewrite i line */
            Vect_rewrite_line(OutMap, features_id[abs(ln_i_id) - 1],
                              ltype_i, line_pnts_i, cats_i);

            features_id[abs(ln_i_id) - 1] = i_ucat + i_rew_lines;
            i_rew_lines += 1;
        }
    }

    /* Create point on every node. */
    /* TODO what if nodes are not used in alayer? 
     */
    for (pivot_node = 1; pivot_node <= n_nodes; pivot_node++) {

        Vect_get_node_coor(InMap, pivot_node, &x, &y, &z);

        Vect_reset_line(line_pnts_i);
        Vect_append_point(line_pnts_i, x, y, z);

        Vect_reset_cats(cats_i);
        Vect_cat_set(cats_i, tucfield, i_ucat);

        Vect_write_line(OutMap, GV_POINT, line_pnts_i, cats_i);
        i_ucat++;
    }

    /* copy rest of features, to output map */
    while ((ltype_i = Vect_read_next_line(InMap, line_pnts_i, cats_i)) > 0) {

        /* line features in alayer are already in output map*/
        if (ltype_i == arc_type && Vect_field_cat_get(cats_i, a_field, &list) != -1)
            continue;

        /* Delete categories in tfield and tucfield if they are in input map defined. */
        Vect_field_cat_del(cats_i, tucfield, -1);   
        Vect_field_cat_del(cats_i, tfield, -1);   

        Vect_write_line(OutMap, GV_POINT, line_pnts_i, cats_i);
    }


    G_free(features_id);

    Vect_destroy_line_struct(line_pnts_i);
    Vect_destroy_line_struct(line_pnts_j);
    Vect_destroy_cats_struct(cats_i);
    Vect_destroy_cats_struct(cats_j);

    db_commit_transaction(driver);
    return;
}