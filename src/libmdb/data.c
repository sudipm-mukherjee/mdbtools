/* MDB Tools - A library for reading MS Access database file
 * Copyright (C) 2000 Brian Bruns
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "mdbtools.h"

#define MDB_DEBUG 0

void mdb_bind_column(MdbTableDef *table, int col_num, void *bind_ptr)
{
MdbColumn *col;

	/* 
	** the column arrary is 0 based, so decrement to get 1 based parameter 
	*/
	col=g_ptr_array_index(table->columns, col_num - 1);
	col->bind_ptr = bind_ptr;
}
int mdb_find_end_of_row(MdbHandle *mdb, int row)
{
int rows, row_end;

	rows = mdb_get_int16(mdb,8);
	if (row==0) 
		row_end = mdb->pg_size - 1; /* end of page */
	else 
		row_end = mdb_get_int16(mdb, (10 + (row-1) * 2)) - 1;

	return row_end;
}
int mdb_read_row(MdbTableDef *table, int pg_num, int row)
{
MdbHandle *mdb = table->entry->mdb;
MdbColumn *col;
int j;
int num_cols, var_cols, fixed_cols;
int row_start, row_end;
int fixed_cols_found, var_cols_found;
int col_start, len;
int eod; /* end of data */
int delflag, lookupflag;
int bitmask_sz;

	row_start = mdb_get_int16(mdb, 10+(row*2)); 
	row_end = mdb_find_end_of_row(mdb, row);

	delflag = lookupflag = 0;
	if (row_start & 0x8000) delflag++;
	if (row_start & 0x4000) lookupflag++;
	row_start &= 0x0FFF; /* remove flags */
#if DEBUG
	fprintf(stdout,"Pg %d Row %d bytes %d to %d %s %s\n", 
		pg_num, row, row_start, row_end,
		lookupflag ? "[lookup]" : "",
		delflag ? "[delflag]" : "");
#endif	
	if (delflag || lookupflag) {
		row_end = row_start-1;
		return 0;
	}

#if MDB_DEBUG
	buffer_dump(mdb->pg_buf, row_start, row_end);
#endif

	/* find out all the important stuff about the row */
	num_cols = mdb->pg_buf[row_start];
	var_cols = 0; /* mdb->pg_buf[row_end-1]; */
	fixed_cols = 0; /* num_cols - var_cols; */
	for (j = 0; j < table->num_cols; j++) {
		col = g_ptr_array_index (table->columns, j);
		if (mdb_is_fixed_col(col)) 
			fixed_cols++;
		else
			var_cols++;
	}
	bitmask_sz = (num_cols - 1) / 8 + 1;
	eod = mdb->pg_buf[row_end-1-var_cols-bitmask_sz];

#if MDB_DEBUG
	fprintf(stdout,"#cols: %-3d #varcols %-3d EOD %-3d\n", 
		num_cols, var_cols, eod);
#endif

	/* data starts at 1 */
	col_start = 1;
	fixed_cols_found = 0;
	var_cols_found = 0;

	/* fixed columns */
	for (j=0;j<table->num_cols;j++) {
		col = g_ptr_array_index(table->columns,j);
		if (mdb_is_fixed_col(col) &&
		    ++fixed_cols_found <= fixed_cols) {
			if (col->bind_ptr) {
				strcpy(col->bind_ptr, 
					mdb_col_to_string(mdb, 
						row_start + col_start,
						col->col_type,
						col->col_size)
					);
			}
#if MDB_DEBUG
			fprintf(stdout,"fixed col %s = %s\n",
				col->name,
				mdb_col_to_string(mdb, 
					row_start + col_start,
					col->col_type,
					0));
#endif
			col_start += col->col_size;
		}
	}

	/* variable columns */
	for (j=0;j<table->num_cols;j++) {
		col = g_ptr_array_index(table->columns,j);
		if (!mdb_is_fixed_col(col) &&
		    ++var_cols_found <= var_cols) {
			col_start = mdb->pg_buf[row_end-bitmask_sz-var_cols_found];

			if (var_cols_found==var_cols) 
				len=eod - col_start;
			else 
				len=mdb->pg_buf[row_end 
					- bitmask_sz 
					- var_cols_found 
					- 1 ] - col_start;

#if MDB_DEBUG
			fprintf(stdout,"coltype %d colstart %d len %d\n",
				col->col_type,
				col_start, 
				len);
#endif
			if (col->bind_ptr) {
				strcpy(col->bind_ptr, 
					mdb_col_to_string(mdb, 
						row_start + col_start,
						col->col_type,
						len)
					);
			}
#if MDB_DEBUG
			fprintf(stdout,"var col %s = %s\n", 
				col->name, 
				mdb_col_to_string(mdb,
					row_start + col_start,
					col->col_type,
					len));
#endif

			col_start += len;
		}
	}

	row_end = row_start-1;
}
int mdb_read_next_dpg(MdbTableDef *table)
{
MdbCatalogEntry *entry = table->entry;
MdbHandle *mdb = entry->mdb;

	do {
		if (!mdb_read_pg(mdb, table->cur_phys_pg++))
			return 0;
	} while (mdb->pg_buf[0]!=0x01 || mdb_get_int32(mdb, 4)!=entry->table_pg);
	return table->cur_phys_pg;
}
int mdb_rewind_table(MdbTableDef *table)
{
	table->cur_pg_num=0;
	table->cur_phys_pg=0;
	table->cur_row=0;
}
int mdb_fetch_row(MdbTableDef *table)
{
MdbHandle *mdb = table->entry->mdb;
int rows;

	if (!table->cur_pg_num) {
		table->cur_pg_num=1;
		table->cur_row=0;
		mdb_read_next_dpg(table);
	}

	rows = mdb_get_int16(mdb,8);
	mdb_read_row(table, 
		table->cur_pg_num, 
		table->cur_row);

	table->cur_row++;
	if (table->cur_row > rows) {
		table->cur_row=0;
		if (!mdb_read_next_dpg(table)) return 0;
	}
	return 1;
}
void mdb_data_dump(MdbTableDef *table)
{
MdbHandle *mdb = table->entry->mdb;
int i, j, pg_num;
int rows;
char *bound_values[256]; /* warning doesn't handle tables > 256 columns.  Can that happen? */

	for (i=0;i<table->num_cols;i++) {
		bound_values[i] = (char *) malloc(256);
		mdb_bind_column(table, i+1, bound_values[i]);
	}
	mdb_rewind_table(table);
	while (mdb_fetch_row(table)) {
		for (j=0;j<table->num_cols;j++) {
			fprintf(stdout, "column %d is %s\n", j+1, bound_values[j]);
		}
	}
#if 0
	for (pg_num=1;pg_num<=table->num_pgs;pg_num++) {
		mdb_read_pg(mdb,table->first_data_pg + pg_num);
		rows = mdb_get_int16(mdb,8);
		fprintf(stdout,"Rows on page %d: %d\n", 
			pg_num + table->first_data_pg, 
			rows);
		for (i=0;i<rows;i++) {
			mdb_read_row(table, table->first_data_pg + pg_num, i);
			for (j=0;j<table->num_cols;j++) {
				fprintf(stdout, "column %d is %s\n", j+1, bound_values[j]);
			}
		}
	}
#endif
	for (i=0;i<table->num_cols;i++) {
		free(bound_values[i]);
	}
}

int mdb_is_fixed_col(MdbColumn *col)
{
	return col->is_fixed;
}
char *mdb_col_to_string(MdbHandle *mdb, int start, int datatype, int size)
{
static char text[256];

	switch (datatype) {
		case MDB_BOOL:
			sprintf(text,"%d", mdb->pg_buf[start]);
			return text;
		break;
		case MDB_INT:
			sprintf(text,"%ld",mdb_get_int16(mdb, start));
			return text;
		break;
		case MDB_LONGINT:
			sprintf(text,"%ld",mdb_get_int32(mdb, start));
			return text;
		break;
		case MDB_TEXT:
			if (size<0) {
				return "(oops)";
			}
			strncpy(text, &mdb->pg_buf[start], size);
			text[size]='\0';
			return text;
		case MDB_MEMO:
			if (size<MDB_MEMO_OVERHEAD) {
				return "(oops)";
			}
			strncpy(text, &mdb->pg_buf[start + MDB_MEMO_OVERHEAD], 
				size - MDB_MEMO_OVERHEAD);
			text[size - MDB_MEMO_OVERHEAD]='\0';
			return text;
		break;
	}
	return NULL;
}
