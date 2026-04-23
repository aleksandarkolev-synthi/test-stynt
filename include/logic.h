/**
 * Logic Layer - Movie Collection Manager
 *
 * Houses sorting, searching and analytical algorithms. The logic layer
 * is the ONLY layer the presentation may call; it delegates storage to
 * the data layer. No OOP: plain enums, structs and free functions.
 */
#ifndef MCM_LOGIC_H
#define MCM_LOGIC_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "data.h"
#include "protocol.h"

namespace mcm::logic {

/**
 * SortKey - chooses which field sortMovies() orders by.
 */
enum class SortKey {
    TITLE,
    YEAR,
    RATING,
    DURATION,
};

/**
 * SortOrder - ascending or descending order.
 */
enum class SortOrder {
    ASCENDING,
    DESCENDING,
};

/**
 * sortMovies - sorts a vector of movies in-place using quick sort.
 *
 * @param movies Sequence to order (mutated).
 * @param key    Field to compare on.
 * @param order  Ascending or descending.
 */
void sortMovies(std::vector<data::Movie>& movies, SortKey key, SortOrder order);

/**
 * linearSearchByTitle - case-insensitive substring search.
 *
 * @param movies    Sequence to scan.
 * @param needle    Substring to look for inside each title.
 * @return Indices of every match (empty if none).
 */
std::vector<std::size_t> linearSearchByTitle(
    const std::vector<data::Movie>& movies,
    const std::string& needle);

/**
 * binarySearchExactTitle - O(log n) lookup for an exact title.
 * The input MUST already be sorted ascending by title.
 *
 * @param sortedMovies Sequence pre-sorted by title ascending.
 * @param title        Exact title to find (case-sensitive).
 * @return Index of the match, or SIZE_MAX if not found.
 */
std::size_t binarySearchExactTitle(
    const std::vector<data::Movie>& sortedMovies,
    const std::string& title);

/**
 * totalDurationRecursive - recursively sums the durationMinutes of every
 * movie referenced by an id list. Demonstrates classic head + tail
 * recursion with a cursor parameter.
 *
 * @param movies       Source snapshot.
 * @param selectedIds  Ids to include in the sum.
 * @param cursor       Current position within selectedIds (start with 0).
 * @return Total minutes across the selection.
 */
long long totalDurationRecursive(
    const std::vector<data::Movie>& movies,
    const std::vector<std::uint64_t>& selectedIds,
    std::size_t cursor);

/**
 * averageRating - arithmetic mean of rating across the entire collection.
 *
 * @param movies Collection snapshot.
 * @return Mean rating (0 if the collection is empty).
 */
float averageRating(const std::vector<data::Movie>& movies);

/**
 * validateMovie - sanity-checks a user-entered movie prior to submission.
 *
 * @param movie          Candidate movie.
 * @param errorMessage   Out-parameter populated with a human message on failure.
 * @return true if the movie is acceptable.
 */
bool validateMovie(const data::Movie& movie, std::string& errorMessage);

/**
 * snapshotCollection - convenience wrapper around data::snapshotMovies.
 * Kept here so presentation never talks to the data layer directly.
 *
 * @param collection Source collection.
 * @return Detached copy of the vector.
 */
std::vector<data::Movie> snapshotCollection(const data::Collection& collection);

/**
 * replaceMirror - atomically replaces the contents of a local mirror with
 * the given movie list and revision counter. Used when the server sends a
 * full snapshot (HELLO / FULL_STATE).
 *
 * @param collection Local mirror to overwrite.
 * @param movies     New contents.
 * @param revision   Server-provided revision counter.
 */
void replaceMirror(data::Collection& collection,
                   const std::vector<data::Movie>& movies,
                   std::uint64_t revision);

/**
 * applyEventToMirror - applies a single server event (added/updated/removed)
 * to a local mirror. Presentation code calls this instead of reaching into
 * the data layer directly.
 *
 * @param collection Local mirror.
 * @param message    Decoded protocol message.
 * @return true if the mirror changed as a result.
 */
bool applyEventToMirror(data::Collection& collection,
                        const protocol::Message& message);

} // namespace mcm::logic

#endif // MCM_LOGIC_H
