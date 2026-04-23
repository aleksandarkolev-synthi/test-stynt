/**
 * Data Layer - Movie Collection Manager
 *
 * Owns the canonical in-memory collection of movies and provides
 * thread-safe CRUD primitives plus file persistence. The layer is
 * purely structural: it exposes a Movie struct, a Collection struct
 * that bundles the vector with its synchronization primitives, and
 * free functions that operate on them.
 *
 * The data layer has NO knowledge of the logic or presentation layers.
 */
#ifndef MCM_DATA_H
#define MCM_DATA_H

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace mcm::data {

constexpr std::size_t MAX_TITLE_LENGTH = 256;
constexpr int MIN_YEAR = 1880;
constexpr int MAX_YEAR = 2200;
constexpr float MIN_RATING = 0.0f;
constexpr float MAX_RATING = 10.0f;

/**
 * Movie - plain-data record describing a single movie.
 */
struct Movie {
    std::uint64_t id = 0;
    std::string title;
    int year = 0;
    float rating = 0.0f;
    int durationMinutes = 0;
};

/**
 * Collection - the canonical movie list plus concurrency state.
 * The mutex protects `movies`, `nextId` and `revision`.
 */
struct Collection {
    std::vector<Movie> movies;
    mutable std::mutex mutex;
    std::uint64_t nextId = 1;
    std::uint64_t revision = 0;
};

/**
 * addMovie - appends a new movie, assigning it a unique id.
 *
 * @param collection Target collection (mutated under lock).
 * @param prototype  Movie payload (the id field is ignored / overwritten).
 * @return The newly-assigned id.
 */
std::uint64_t addMovie(Collection& collection, const Movie& prototype);

/**
 * addMovieWithId - inserts a movie that already carries an id (e.g. when
 * a client mirrors a server broadcast). Bumps nextId as needed.
 *
 * @param collection Target collection.
 * @param movie      Complete movie (id must be non-zero).
 * @return true on success, false if the id already exists.
 */
bool addMovieWithId(Collection& collection, const Movie& movie);

/**
 * updateMovie - replaces an existing movie with a matching id.
 *
 * @param collection Target collection.
 * @param movie      Updated record (its id identifies the target).
 * @return true if a movie with the given id existed and was replaced.
 */
bool updateMovie(Collection& collection, const Movie& movie);

/**
 * removeMovie - deletes the movie with the given id.
 *
 * @param collection Target collection.
 * @param id         Identifier of the movie to remove.
 * @return true if a movie was removed.
 */
bool removeMovie(Collection& collection, std::uint64_t id);

/**
 * snapshotMovies - returns a copy of the current vector for read-only use
 * by the logic/presentation layer without holding the lock afterwards.
 *
 * @param collection Source collection.
 * @return Detached copy of the movies vector.
 */
std::vector<Movie> snapshotMovies(const Collection& collection);

/**
 * currentRevision - returns the monotonic revision counter. Useful for
 * detecting whether the snapshot presentation currently holds is stale.
 *
 * @param collection Source collection.
 * @return Revision number.
 */
std::uint64_t currentRevision(const Collection& collection);

/**
 * findIndexById - linear lookup of a movie's position inside a snapshot.
 *
 * @param movies Sequence to search.
 * @param id     Identifier to find.
 * @return Index of the match, or SIZE_MAX if not present.
 */
std::size_t findIndexById(const std::vector<Movie>& movies, std::uint64_t id);

/**
 * loadFromFile - populates the collection from a JSON file on disk.
 * Existing contents are replaced.
 *
 * @param collection Destination collection.
 * @param path       Filesystem path to read.
 * @return true on success.
 */
bool loadFromFile(Collection& collection, const std::string& path);

/**
 * saveToFile - writes the collection to disk as JSON.
 *
 * @param collection Source collection.
 * @param path       Filesystem path to write.
 * @return true on success.
 */
bool saveToFile(const Collection& collection, const std::string& path);

} // namespace mcm::data

#endif // MCM_DATA_H
