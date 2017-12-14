#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    disk_sector_t inode_sector;         /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) 
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), LV2, DIR);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

void dir_set_parent (disk_sector_t child, disk_sector_t parent)
{
  struct inode *child_inode = inode_open (child);
  child_inode->data.parent = parent;
  write_buff (child_inode->sector, &child_inode->data, 0, DISK_SECTOR_SIZE);
  inode_close (child_inode);
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  if(strcmp(name, ".") == 0){
    *inode = inode_reopen(dir->inode);
    return true;
  }
  if(strcmp(name, "..") == 0){
    *inode = inode_open(dir->inode->data.parent);
    return true;
  }
  if (lookup (dir, name, &e, NULL)){
    *inode = inode_open (e.inode_sector);
  }
  else
    *inode = NULL;
  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) 
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

struct dir *parse_directory (char *path)
{
    struct dir *curr;
    char *buff, *token, *save_ptr;
    int length = strlen (path);
    struct inode *inode;

    if(path == NULL || path == '\0') return NULL;

    buff = malloc (length+1);
    memcpy (buff, path, length+1);
    if (path[0] == '/') { // 절대경로
        curr = dir_open_root ();
        token = strtok_r (buff+1, "/", &save_ptr);
    }
    else { // 상대경로
        struct thread *t = thread_current();

        if (t->curr_dir == NULL) 
            curr = dir_open_root ();
        else
            curr = dir_reopen (t->curr_dir);

        token = strtok_r (buff, "/", &save_ptr);

        if(strcmp(token, "..") == 0) // 부모를 찾을 경우 상위 디렉토리 로드한다.
        {
            inode = inode_open((dir_get_inode(curr))->data.parent);
            dir_close(curr);
            curr = dir_open(inode);
        }
    }
    printf("path : %s\n", token);

    // 파싱했는데 NULL이면 현재 경로 리턴
    if(token == NULL) return curr;

    while(true)
    {
        token = strtok_r(NULL, "/", &save_ptr);
        printf("path : %s\n", token);

        if(token == NULL) break;
        
        if(strcmp(token, "..") == 0) // 부모를 찾을 경우 상위 디렉토리 로드한다.
        {
            inode = inode_open((dir_get_inode(curr))->data.parent);
            dir_close(curr);
            curr = dir_open(inode);
        }
        else
        {
            inode = NULL;
            if(dir_lookup(curr, token, &inode))
            {
                dir_close(curr);
                if(inode->data.is_dir == DIR)
                {
                    curr = dir_open(inode);
                }
                else
                {
                    inode_close(inode);
                    return NULL;
                }
            }
            else
            {
                dir_close(curr);
                return NULL;
            }
        }
    }

  // 선생님
  /*char *word;
  while (1) {
    word = token;
    if(word == NULL) break;
    token = strtok_r(NULL, "/", &save_point);
    if(token == NULL) break;
    if(strcmp(word,"")==0);
    else if(strcmp(word,".")==0);
    else if(strcmp(word,"..")==0){
      inode = inode_open((dir_get_inode(curr))->data.parent);
      dir_close(curr);
      curr = dir_open(inode);
    }
    else{
      inode = NULL;
      if(dir_lookup(curr, word, &inode)){
        dir_close(curr);
        if(inode->data.is_dir == DIR)
          curr = dir_open(inode);
        else{
          inode_close(inode);
          free(buff_first);
          return NULL;
        }
      }
      else{
        dir_close(curr);
        free(buff_first);
        return NULL;
      }
    }
  }*/
    // for (token; token; token = strtok_r (NULL, "/", &save_point))
  // {
  //   if (!dir_lookup (curr, token, &inode)) {
  //     dir_close (curr);
  //     free (buff_first);
  //     return NULL;
  //   }

  //   if (inode->data.is_dir != DIR) {
  //     dir_close (curr);
  //     free (buff_first);
  //     return NULL;
  //   }

  //   dir_close (curr);
  //   curr = dir_open (inode);
  // }
    return curr;
}

char *parse_name (char *path)
{
    char *buff, token, *save_ptr;

    int length = strlen(length);
    buff = malloc (length+1);
    memcpy (buff, path, length+1);

    char *name = NULL;
    for (token = strtok_r (buff, "/", &save_ptr); token != NULL; token = strtok_r (NULL, "/", &save_ptr))
    {
        name = token;
    }
    
    free(buff);

    if(name == NULL){
        char *file_name = malloc(1);
        *file_name = '\0';
        return file_name;
    }
    else
    {
        length = strlen(name);
        char *file_name = malloc(length + 1);
        memcpy(file_name, name, length + 1);
        return file_name;
    }
}